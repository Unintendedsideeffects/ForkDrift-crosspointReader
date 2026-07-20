#include "OpdsBookBrowserActivity.h"

#include <Epub.h>
#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <OpenSearchParser.h>
#include <WiFi.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "SpiBusMutex.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "core/features/KoreaderOpdsBridge.h"
#include "fontIds.h"
#include "network/http/HttpDownloader.h"
#include "util/LibraryShelfStore.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace {
constexpr int PAGE_ITEMS = 23;

// Href sentinel marking the synthetic "Sync" row prepended to the cached catalog
// (a NAVIGATION entry the Confirm handler special-cases instead of fetching). The
// control char keeps it from ever colliding with a real OPDS href.
constexpr const char* kSyncSentinelHref = "\x01opds-sync";

// Long-press threshold for Confirm = SYNC.
constexpr unsigned long kSyncLongPressMs = 600;

OpdsFilename::Format configuredOpdsFilenameFormat() {
  return SETTINGS.opdsFilenameFormat == CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR
             ? OpdsFilename::Format::TitleAuthor
             : OpdsFilename::Format::AuthorTitle;
}
}  // namespace

void OpdsBookBrowserActivity::onEnter() {
  Activity::onEnter();

  entries.clear();
  navigationHistory.clear();
  searchTemplate = "";
  currentPath = "";
  selectorIndex = 0;
  consumeConfirm = false;
  consumeBack = false;
  errorMessage.clear();
  pendingAction = PendingAction::None;
  showingCachedCatalog = false;
  confirmPressStartMs = 0;

  // Cache-first (plan 023 Phase 1): if the root shelf for this server is persisted,
  // render it offline immediately — no WiFi on enter. The user connects explicitly
  // via SYNC, or implicitly when acting on an entry (connect-on-demand). With no
  // cache, fall back to the original online flow.
  if (loadCachedCatalog()) {
    showingCachedCatalog = true;
    state = BrowserState::BROWSING;
    statusMessage.clear();
    requestUpdate();
    return;
  }

  state = BrowserState::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
  Activity::onExit();
  entries.clear();
  navigationHistory.clear();
  recoverHeapAfterWifi("OPDS");
}

void OpdsBookBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION || state == BrowserState::SEARCH_INPUT) {
    return;
  }

  if (consumeConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return;
  }
  if (consumeBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    consumeBack = false;
    return;
  }

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed(currentPath);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state == BrowserState::CHECK_WIFI ? onGoHome() : navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) return;

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      confirmPressStartMs = millis();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const bool longPress = confirmPressStartMs != 0 && (millis() - confirmPressStartMs) >= kSyncLongPressMs;
      confirmPressStartMs = 0;
      if (longPress) {
        syncCatalog();  // long-press Confirm = SYNC (connect + refresh), from any row
      } else if (!entries.empty()) {
        const auto& entry = entries[selectorIndex];
        if (entry.type == OpdsEntryType::NAVIGATION && entry.href == kSyncSentinelHref) {
          syncCatalog();  // the synthetic "Sync" row
        } else if (entry.type == OpdsEntryType::BOOK) {
          downloadBook(entry);
        } else {
          navigateToEntry(entry);
        }
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (!searchTemplate.empty() && selectorIndex == 0) launchSearch();
    }

    if (!entries.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, entries.size());
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, entries.size());
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void OpdsBookBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Show server name in header if available, otherwise generic title
  const char* headerTitle = server.name.empty() ? tr(STR_OPDS_BROWSER) : server.name.c_str();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadProgress,
                          downloadTotal);
    }
    renderer.displayBuffer();
    return;
  }

  const char* confirmLabel =
      (!entries.empty() && entries[selectorIndex].type == OpdsEntryType::BOOK) ? tr(STR_DOWNLOAD) : tr(STR_OPEN);
  const char* searchLabel = (!searchTemplate.empty() && selectorIndex == 0) ? tr(STR_SEARCH) : tr(STR_DIR_UP);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, searchLabel, tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& entry = entries[i];
      std::string displayText = (entry.type == OpdsEntryType::NAVIGATION) ? "> " + entry.title : entry.title;
      if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) displayText += " - " + entry.author;
      auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (server.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  const auto creds = core::effectiveOpdsCredentials(server);
  std::string url = (path.find("http") == 0) ? path : UrlUtils::buildUrl(server.url, path);

  // If this is an HTTPS fetch and the heap is too fragmented to sustain a TLS
  // session, restart now rather than letting mbedTLS fail mid-request. The
  // silent restart recovers the heap; onExit's recoverHeapAfterWifi would do
  // the same thing, but only after the user dismisses an error screen.
  if (UrlUtils::isHttpsUrl(url) && ESP.getFreeHeap() < HttpDownloader::MIN_HEAP_FOR_HTTPS) {
    LOG_ERR("OPDS", "Heap too low for HTTPS feed (%u < %u); silent restart", ESP.getFreeHeap(),
            HttpDownloader::MIN_HEAP_FOR_HTTPS);
    silentRestart();
    return;  // unreachable
  }

  LOG_DBG("OPDS", "Fetching: %s", url.c_str());

  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(url, stream, creds.username, creds.password)) {
      state = BrowserState::ERROR;
      errorMessage = tr(STR_FETCH_FEED_FAILED);
      if (creds.username.empty()) {
        int status = HttpDownloader::probeUrl(url, creds.username, creds.password);
        if (status == 401 || status == 403) {
          errorMessage = tr(STR_OPDS_AUTH_REQUIRED);
        }
      }
      requestUpdate();
      return;
    }
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  searchTemplate = parser.getSearchTemplate();
  if (searchTemplate.empty()) {
    // OPDS 1.2 / OpenSearch: the feed pointed at a description document rather
    // than inlining a {searchTerms} template. Fetch & parse it to recover the
    // real search URL. The doc is small and fixed-size, so a one-shot
    // string fetch is used instead of streaming. Skipped entirely on OPDS 1.1
    // servers (no description URL) — no extra request in that case.
    const std::string& descRef = parser.getSearchDescriptionUrl();
    if (!descRef.empty()) {
      const std::string descUrl = (descRef.find("http") == 0) ? descRef : UrlUtils::buildUrl(url, descRef);
      std::string descDoc;
      if (HttpDownloader::fetchUrl(descUrl, descDoc, creds.username, creds.password)) {
        searchTemplate = OpenSearchParser::extractSearchTemplate(descDoc);
        LOG_DBG("OPDS", "OpenSearch search template: %s", searchTemplate.c_str());
      }
    }
  }
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  entries = std::move(parser).getEntries();

  const auto& servers = OPDS_STORE.getServers();
  const bool isRootFeed = navigationHistory.empty() && path.empty();
  const bool isFirstServer = !servers.empty() && (server.url == servers[0].url || server.name == servers[0].name);
  if (isRootFeed && isFirstServer) {
    std::vector<LibraryShelfEntry> shelfEntries;
    shelfEntries.reserve(6);
    for (const auto& entry : entries) {
      if (entry.type != OpdsEntryType::BOOK) {
        continue;
      }
      shelfEntries.push_back({entry.title, entry.author, entry.href});
      if (shelfEntries.size() >= 6) {
        break;
      }
    }
    SpiBusMutex::Guard guard;
    LIBRARY_SHELF.replaceEntries(server.name, std::move(shelfEntries));
  }

  if (!prevUrl.empty()) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""});
  }

  selectorIndex = 0;
  state = entries.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (entries.empty()) errorMessage = tr(STR_NO_ENTRIES);
  requestUpdate();
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  if (deferUntilOnline(PendingAction::Navigate, entry, "")) {
    return;  // connect on demand, then re-run from onWifiSelectionComplete
  }
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  entries.clear();
  selectorIndex = 0;
  requestUpdate(true);
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    onGoHome();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    entries.clear();
    selectorIndex = 0;
    requestUpdate();
    fetchFeed(currentPath);
  }
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  if (deferUntilOnline(PendingAction::Download, book, "")) {
    return;  // connect on demand, then re-run from onWifiSelectionComplete
  }
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = downloadTotal = 0;
  requestUpdate(true);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);
  std::string filename = "/" + OpdsFilename::format(book.title, book.author, configuredOpdsFilenameFormat(), ".epub");
  LOG_DBG("OPDS", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  const auto creds = core::effectiveOpdsCredentials(server);
  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        requestUpdate(true);
      },
      nullptr, creds.username, creds.password);

  if (result == HttpDownloader::OK) {
#if ENABLE_EPUB_SUPPORT
    Epub epub(filename, "/.crosspoint");
    epub.clearCache();
#endif
    state = BrowserState::BROWSING;
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
  }
  requestUpdate();
}

void OpdsBookBrowserActivity::launchSearch() {
  consumeConfirm = true;
  state = BrowserState::SEARCH_INPUT;
  requestUpdate();

  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    state = BrowserState::BROWSING;
    if (!result.isCancelled) {
      performSearch(std::get<KeyboardResult>(result.data).text);
    } else {
      requestUpdate();
    }
  });
}

void OpdsBookBrowserActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }
  if (deferUntilOnline(PendingAction::Search, OpdsEntry{}, query)) {
    return;  // connect on demand, then re-run from onWifiSelectionComplete
  }

  auto urlEncode = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        out += static_cast<char>(c);
      else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
    return out;
  };

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), urlEncode(query));

  // Strip remaining optional parameters {name?}
  size_t startPos = 0;
  while ((startPos = url.find('{', startPos)) != std::string::npos) {
    size_t endPos = url.find('}', startPos);
    if (endPos != std::string::npos) {
      if (endPos > startPos + 1 && url[endPos - 1] == '?') {
        url.erase(startPos, endPos - startPos + 1);
      } else {
        startPos = endPos + 1;
      }
    } else {
      break;
    }
  }

  // Tidy dangling '&'/'?' separators left by emptied params
  size_t qPos = url.find('?');
  if (qPos != std::string::npos) {
    std::string queryStr = url.substr(qPos + 1);
    std::string baseUrl = url.substr(0, qPos);
    std::vector<std::string> params;
    size_t scanPos = 0;
    while (true) {
      size_t nextAmp = queryStr.find('&', scanPos);
      std::string param =
          (nextAmp == std::string::npos) ? queryStr.substr(scanPos) : queryStr.substr(scanPos, nextAmp - scanPos);
      bool isEmpty = false;
      if (param.empty()) {
        isEmpty = true;
      } else {
        size_t eqPos = param.find('=');
        if (eqPos != std::string::npos) {
          if (eqPos == param.size() - 1) {
            isEmpty = true;
          }
        }
      }
      if (!isEmpty) {
        params.push_back(param);
      }
      if (nextAmp == std::string::npos) break;
      scanPos = nextAmp + 1;
    }
    std::string newQuery;
    for (const auto& p : params) {
      if (!newQuery.empty()) newQuery += "&";
      newQuery += p;
    }
    if (!newQuery.empty()) {
      url = baseUrl + "?" + newQuery;
    } else {
      url = baseUrl;
    }
  }

  while (url.find("&&") != std::string::npos) {
    url.replace(url.find("&&"), 2, "&");
  }
  while (url.find("?&") != std::string::npos) {
    url.replace(url.find("?&"), 2, "?");
  }
  if (!url.empty() && url.back() == '&') {
    url.pop_back();
  }
  if (!url.empty() && url.back() == '?') {
    url.pop_back();
  }

  navigationHistory.push_back(currentPath);
  currentPath = url;

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  requestUpdate(true);
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}

void OpdsBookBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    pendingAction = PendingAction::None;
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
    return;
  }

  // Now online — run whatever action was deferred for connect-on-demand. Copy the
  // params out first: the action methods mutate `entries`/state and re-enter cleanly.
  const PendingAction action = pendingAction;
  pendingAction = PendingAction::None;
  switch (action) {
    case PendingAction::Download: {
      const OpdsEntry book = pendingEntry;
      downloadBook(book);
      break;
    }
    case PendingAction::Navigate: {
      const OpdsEntry entry = pendingEntry;
      navigateToEntry(entry);
      break;
    }
    case PendingAction::Search: {
      const std::string query = pendingQuery;
      performSearch(query);
      break;
    }
    case PendingAction::None:
    default:
      // Initial connect / SYNC refresh: (re)load the current path (root for SYNC).
      state = BrowserState::LOADING;
      statusMessage = tr(STR_LOADING);
      requestUpdate(true);
      fetchFeed(currentPath);
      break;
  }
}

bool OpdsBookBrowserActivity::isOnline() const {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

bool OpdsBookBrowserActivity::loadCachedCatalog() {
  // Only the first configured server's root shelf is persisted (matches fetchFeed's
  // write guard), so only offer the cache for that server.
  const auto& servers = OPDS_STORE.getServers();
  const bool isFirstServer = !servers.empty() && (server.url == servers[0].url || server.name == servers[0].name);
  if (!isFirstServer) {
    return false;
  }
  const std::vector<LibraryShelfEntry> shelf = LIBRARY_SHELF.getSnapshot();
  if (shelf.empty()) {
    return false;
  }
  entries.clear();
  entries.reserve(shelf.size() + 1);
  // Synthetic "Sync" row at the top = the menu-item SYNC affordance (long-press
  // Confirm does the same from any row). It is a NAVIGATION entry the Confirm
  // handler special-cases via its sentinel href instead of fetching.
  entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_SYNC), "", kSyncSentinelHref, ""});
  std::transform(shelf.begin(), shelf.end(), std::back_inserter(entries), [](const LibraryShelfEntry& e) {
    return OpdsEntry{OpdsEntryType::BOOK, e.title, e.author, e.href, ""};
  });
  selectorIndex = 0;
  return true;
}

void OpdsBookBrowserActivity::syncCatalog() {
  // SYNC = connect-on-demand, then force-refresh the root catalog.
  showingCachedCatalog = false;
  navigationHistory.clear();
  currentPath = "";
  selectorIndex = 0;
  if (deferUntilOnline(PendingAction::None, OpdsEntry{}, "")) {
    return;  // refresh happens after WiFi connects (onWifiSelectionComplete, None branch)
  }
  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  entries.clear();
  requestUpdate(true);
  fetchFeed("");
}

bool OpdsBookBrowserActivity::deferUntilOnline(const PendingAction action, const OpdsEntry& entry,
                                               const std::string& query) {
  if (isOnline()) {
    return false;
  }
  pendingAction = action;
  pendingEntry = entry;
  pendingQuery = query;
  launchWifiSelection();
  return true;
}
