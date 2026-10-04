#include "StatsUploadActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <I18n.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <LibraryBuilder.h>
#include <Memory.h>
#include <WiFi.h>
#include <Xtc.h>

#include <cstring>

#include "HalClock.h"
#include "SdCardFontSystem.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/BookStatsTracking.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/KOReaderSyncActivity.h"
#include "activities/reader/StatsUploadPayload.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/ReadingSyncUpload.h"
#include "network/WifiUtils.h"
#include "util/InputReleaseGuard.h"

void StatsUploadActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseForNetwork(renderer);
  initialConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  if (folder.empty() && !SETTINGS.shouldTrackReadingStats())
    fail(tr(STR_STATS_UPLOAD_DISABLED));
  else if (!KOREADER_STORE.hasCredentials())
    fail(tr(STR_SET_CREDENTIALS_FIRST));
  requestUpdate();
}

void StatsUploadActivity::closeTransfer() {
  index.close();
  client.reset();
  payload.reset();
  folderOffsets.reset();
  folderBooks.reset();
  if (ownsWifi) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    ownsWifi = false;
  }
}

void StatsUploadActivity::onExit() {
  closeTransfer();
  Activity::onExit();
}

void StatsUploadActivity::fail(const char* text) {
  LOG_ERR("StatsSync", "Upload stopped: %s", text);
  {
    RenderLock lock(*this);
    message = text;
    state = State::Failed;
  }
  closeTransfer();
  requestUpdate();
}

void StatsUploadActivity::failBook(std::string&& path) {
  LOG_ERR("StatsSync", "Book sync failed: %s", path.c_str());
  {
    RenderLock lock(*this);
    // EPUB loading may have run out of memory. Reuse the path's existing
    // allocation for the heading instead of allocating an error string.
    message = std::move(path);
    message.erase(0, message.find_last_of('/') + 1);
    ++failed;
    state = State::BookFailed;
  }
  // Keep the folder iterator and connection alive until Skip book or Back.
  requestUpdate();
}

void StatsUploadActivity::start() {
  if (folder.empty()) {
    // One reusable 1.5KB request buffer on the heap, not the main task's stack.
    payload = makeUniqueNoThrow<char[]>(StatsUploadPayload::CAPACITY);
    client = makeUniqueNoThrow<StatsUploadClient>();
    if (!payload || !client || !StatsUploadClient::deviceId(deviceId, sizeof(deviceId))) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
  }
  if (!folder.empty()) {
    folderBooks = makeUniqueNoThrow<FolderBookIterator>(folder);
    if (!folderBooks) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
  }
  libraryAvailable = folder.empty() ? index.open(library::libraryIndexPath()) : true;
  if (folder.empty() && libraryAvailable && index.bookCount() > 0) {
    // Fixed 512-byte index accelerator: allocated only during manual upload,
    // avoiding a folder-table scan for every book without growing with the library.
    folderOffsets = makeUniqueNoThrow<uint32_t[]>(library::LibraryIndexFile::FOLDER_CHECKPOINT_COUNT);
    if (!folderOffsets) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
    if (!index.buildFolderCheckpoints(folderOffsets.get(), folderStride)) {
      fail(tr(STR_STATS_UPLOAD_LIBRARY));
      return;
    }
  }
  if (!hasActiveStationWifiConnection()) {
    ownsWifi = true;
    {
      RenderLock lock(*this);
      state = State::Wifi;
    }
    auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
    if (!wifi) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
    startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
      if (result.isCancelled) {
        finish();
        return;
      }
      if (!hasActiveStationWifiConnection()) {
        fail(tr(STR_WIFI_CONN_FAILED));
        return;
      }
      {
        RenderLock lock(*this);
        state = State::Uploading;
      }
      requestUpdate();
    });
  } else {
    {
      RenderLock lock(*this);
      state = State::Uploading;
    }
    requestUpdate();
  }
}

bool StatsUploadActivity::send(const char* endpoint, const bool book) {
  const auto result = client->put(endpoint, payload.get(), book);
  if (result == StatsUploadClient::Result::Ok) return true;
  fail(StatsUploadClient::errorString(result));
  return false;
}

void StatsUploadActivity::finishUpload() {
  LOG_INF("StatsSync", "Finished: synced=%u skipped=%u failed=%u", static_cast<unsigned>(uploaded),
          static_cast<unsigned>(skipped), static_cast<unsigned>(failed));
  if (!folder.empty())
    LOG_INF("StatsSync", "Extras: global=%d stats=%u statsFailed=%u clippings=%u clippingsFailed=%u",
            static_cast<int>(globalResult), static_cast<unsigned>(statsUploaded), static_cast<unsigned>(statsFailed),
            static_cast<unsigned>(clippingsUploaded), static_cast<unsigned>(clippingsFailed));
  closeTransfer();
  {
    RenderLock lock(*this);
    state = State::Done;
    message = libraryAvailable ? tr(STR_DONE) : tr(STR_STATS_UPLOAD_LIBRARY);
  }
  requestUpdate();
}

void StatsUploadActivity::uploadNext() {
  if (!folder.empty()) {
    if (!globalAttempted) {
      // Batch ownership prevents books that skip/fail from starving overall stats,
      // and prevents retries of accepted global snapshots after a per-book failure.
      globalAttempted = true;
      if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
        LOG_ERR("StatsSync", "Cannot render overall stats upload screen");
        {
          RenderLock lock(*this);
          globalResult = StatsUploadClient::Result::InvalidResponse;
        }
        requestUpdate();
        return;
      }
#ifndef SIMULATOR
      halClock.syncSystemTimeFromNTP();
#endif
      const auto result =
          KOREADER_STORE.getSyncStats() ? ReadingSyncUpload::globalStats() : StatsUploadClient::Result::Skipped;
      {
        RenderLock lock(*this);
        globalResult = result;
      }
      requestUpdate();
      return;
    }
    uploadFolderBook();
    return;
  }
  if (!globalAttempted) {
    globalAttempted = true;
    const auto result = ReadingSyncUpload::globalStats(*client, payload.get(), StatsUploadPayload::CAPACITY, deviceId);
    if (StatsUploadClient::failed(result)) {
      fail(StatsUploadClient::errorString(result));
      return;
    }
    {
      RenderLock lock(*this);
      globalUploaded = result == StatsUploadClient::Result::Ok;
    }
    requestUpdate();
    return;
  }
  if (!libraryAvailable || ordinal >= index.bookCount()) {
    finishUpload();
    return;
  }
  library::ClixRecord record;
  std::string path;
  if (!index.readRecord(ordinal++, record) || !index.readPath(record, path, folderOffsets.get(), folderStride)) {
    fail(tr(STR_STATS_UPLOAD_LIBRARY));
    return;
  }
  const bool epub = FsHelpers::hasEpubExtension(path);
  const bool xtc = FsHelpers::hasXtcExtension(path);
  if (!epub && !xtc) return;
  const std::string cache =
      epub ? Epub::resolveCachePathForFilePath(path, "/.crosspoint") : Xtc(path, "/.crosspoint").getCachePath();
  BookReadingStats stats;
  if (!Storage.exists(path.c_str()) || !BookStatsTracking::isEnabled(cache) ||
      !BookReadingStats::loadForUpload(cache, stats)) {
    {
      RenderLock lock(*this);
      ++skipped;
    }
    requestUpdate();
    return;
  }
  const auto document = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
                            ? KOReaderDocumentId::calculateFromFilename(path)
                            : KOReaderDocumentId::calculate(path);
  if (!StatsUploadPayload::book(payload.get(), StatsUploadPayload::CAPACITY, deviceId, document.c_str(), stats)) {
    fail(tr(STR_SYNC_FAILED_MSG));
    return;
  }
  if (!send("/api/v1/stats/books", true)) return;
  {
    RenderLock lock(*this);
    ++uploaded;
  }
  requestUpdate();
}

void StatsUploadActivity::recordExtras(const bool statsOk, const bool clippingsOk, const bool statsError,
                                       const bool clippingsError) {
  RenderLock lock(*this);
  statsUploaded += statsOk;
  clippingsUploaded += clippingsOk;
  statsFailed += statsError;
  clippingsFailed += clippingsError;
}

bool StatsUploadActivity::uploadFolderExtras(const std::string& path) {
  if (!KOREADER_STORE.getSyncStats() && !KOREADER_STORE.getSyncClippings()) return true;
  const auto document = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
                            ? KOReaderDocumentId::calculateFromFilename(path)
                            : KOReaderDocumentId::calculate(path);
  if (document.empty()) return false;
  const auto result = ReadingSyncUpload::extras(path, document);
  recordExtras(result.stats == StatsUploadClient::Result::Ok, result.clippings == StatsUploadClient::Result::Ok,
               StatsUploadClient::failed(result.stats), StatsUploadClient::failed(result.clippings));
  return result.success();
}

void StatsUploadActivity::uploadFolderBook() {
  std::string path;
  const auto next = folderBooks->next(path);
  if (next == FolderBookIterator::Result::Done) {
    finishUpload();
    return;
  }
  if (next == FolderBookIterator::Result::Error) {
    fail(tr(STR_FOLDER_SYNC_READ_FAILED));
    return;
  }
  if (next != FolderBookIterator::Result::Book) return;
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    fail(tr(STR_SYNC_FAILED_MSG));
    return;
  }
  {
    // Rebuild missing metadata (for example after moving a book into Read),
    // but release it before the child makes TLS requests.
    auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
    if (!epub) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
    EpubReaderUtils::Progress saved;
    if (!EpubReaderUtils::loadProgress(*epub, saved) || !saved.hasPageCount || saved.pageCount < 1 ||
        saved.pageNumber >= saved.pageCount) {
      LOG_INF("StatsSync", "Skipping EPUB progress without usable saved position: %s", path.c_str());
      epub.reset();
      if (!uploadFolderExtras(path)) {
        failBook(std::move(path));
        return;
      }
      {
        RenderLock lock(*this);
        ++skipped;
      }
      requestUpdate();
      return;
    }
    if (!epub->load(true, true, Epub::XLocationLoadMode::Skip, true) || saved.spineIndex < 0 ||
        saved.spineIndex >= epub->getSpineItemsCount()) {
      epub.reset();
      uploadFolderExtras(path);
      failBook(std::move(path));
      return;
    }
  }
  auto sync = makeUniqueNoThrow<KOReaderSyncActivity>(renderer, mappedInput, path, KOREADER_STORE.getMatchMethod(),
                                                      SETTINGS.orientation, true, false);
  if (!sync) {
    fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
    return;
  }
  {
    RenderLock lock(*this);
    state = State::SyncingBook;
  }
  startActivityForResult(std::move(sync), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    const auto* synced = std::get_if<ProgressSyncResult>(&result.data);
    if (!synced) {
      fail(tr(STR_SYNC_FAILED_MSG));
      return;
    }
    recordExtras(synced->statsUploaded, synced->clippingsUploaded, synced->statsFailed, synced->clippingsFailed);
    {
      RenderLock lock(*this);
      if (synced->progressSucceeded) ++uploaded;
      if (!synced->success) ++failed;
      state = State::Uploading;
    }
    requestUpdate();
  });
}

void StatsUploadActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm, initialConfirm))
    return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      TouchHeaderBackButton::wasTapped(mappedInput, renderer)) {
    finishAfterBackPress();
    return;
  }
  if (state == State::Ready || state == State::BookFailed) {
    int x = 0, y = 0;
    bool uploadTapped = false;
    if (mappedInput.wasScreenTapped(x, y)) {
      const auto area = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const auto layout =
          TouchActionButtons::vertical(Rect{area.x + 20, area.y + area.height - 80, area.width - 40, 56}, 1);
      uploadTapped = TouchActionButtons::indexAt(layout, x, y) == 0;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || uploadTapped) {
      if (state == State::Ready) {
        start();
      } else {
        {
          RenderLock lock(*this);
          state = State::Uploading;
        }
        requestUpdate();
      }
    }
  } else if (state == State::Uploading) {
    // One request per loop permits cancellation between books. No background
    // task or persistent upload queue can start networking outside this action.
    if (folder.empty()) requestUpdateAndWait();
    uploadNext();
  }
}

void StatsUploadActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const char* heading =
      state == State::BookFailed ? message.c_str() : (folder.empty() ? tr(STR_STATS_UPLOAD) : tr(STR_FOLDER_SYNC));
  if (mappedInput.hasTouchHardware())
    TouchHeaderBackButton::draw(renderer, header, heading, false);
  else
    GUI.drawHeader(renderer, header, heading);
  const auto area = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int y = area.y + area.height / 3;
  const char* text = state == State::Ready
                         ? (folder.empty() ? tr(STR_STATS_UPLOAD_CONFIRM) : tr(STR_FOLDER_SYNC_CONFIRM))
                     : state == State::Uploading  ? tr(STR_LOADING)
                     : state == State::BookFailed ? tr(STR_SYNC_FAILED_MSG)
                                                  : message.c_str();
  UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, y, text, 3, true, EpdFontFamily::REGULAR, 4);
  if (state == State::Ready) {
    const auto url = folder.empty() ? KOREADER_STORE.getBaseUrl() : folder + "\n" + KOREADER_STORE.getBaseUrl();
    UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, y + 80, url.c_str(), 3, true,
                                     EpdFontFamily::REGULAR, 4);
  } else {
    char counts[160];
    if (folder.empty())
      snprintf(counts, sizeof(counts), tr(STR_STATS_UPLOAD_COUNTS), globalUploaded ? 1u : 0u,
               static_cast<unsigned>(uploaded), static_cast<unsigned>(skipped));
    else
      snprintf(counts, sizeof(counts), tr(STR_FOLDER_SYNC_COUNTS), static_cast<unsigned>(uploaded),
               static_cast<unsigned>(skipped), static_cast<unsigned>(failed));
    if (folder.empty()) {
      char* countLine = counts;
      const int countLineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
      for (int line = 0; line < 3; ++line) {
        char* nextLine = std::strchr(countLine, '\n');
        if (nextLine) *nextLine = '\0';
        UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, y + 80 + line * countLineHeight, countLine, 1,
                                         true, EpdFontFamily::REGULAR, 4);
        if (!nextLine) break;
        countLine = nextLine + 1;
      }
    } else {
      UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, y + 80, counts, 3, true, EpdFontFamily::REGULAR,
                                       4);
    }
    if (!folder.empty()) {
      char extras[240];
      snprintf(extras, sizeof(extras), "%s: %s\n%s: %u (%u %s)\n%s: %u (%u %s)", tr(STR_ALL_TIME_STATS),
               StatsUploadClient::resultString(globalResult), tr(STR_READING_STATS),
               static_cast<unsigned>(statsUploaded), static_cast<unsigned>(statsFailed), tr(STR_FAILED_LOWER),
               tr(STR_CLIPPINGS), static_cast<unsigned>(clippingsUploaded), static_cast<unsigned>(clippingsFailed),
               tr(STR_FAILED_LOWER));
      UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, y + 140, extras, 6, true, EpdFontFamily::REGULAR,
                                       4);
    }
  }
  const char* action = state == State::Ready        ? (folder.empty() ? tr(STR_UPLOAD) : tr(STR_SYNC_PROGRESS))
                       : state == State::BookFailed ? tr(STR_SKIP_BOOK)
                                                    : "";
  if (action[0] && mappedInput.hasTouchHardware()) {
    const auto layout =
        TouchActionButtons::vertical(Rect{area.x + 20, area.y + area.height - 80, area.width - 40, 56}, 1);
    const char* labels[] = {action};
    TouchActionButtons::draw(renderer, layout, labels, 0);
  }
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), action, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
