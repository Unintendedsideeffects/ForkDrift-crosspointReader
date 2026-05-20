#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>
#include <WiFi.h>
#include <Xtc.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <numeric>
#include <optional>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SpiBusMutex.h"
#include "activities/TaskShutdown.h"
#include "components/ScreenComponents.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "core/features/FeatureModules.h"
#include "core/registries/HomeActionRegistry.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"
#include "network/BackgroundWifiService.h"
#include "util/BookProgressDataStore.h"
#include "util/ForkDriftNavigation.h"
#include "util/RecentBooksStore.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t CAROUSEL_CACHE_MAGIC = 0x43434152;  // "CCAR"
constexpr uint16_t CAROUSEL_CACHE_VERSION = 2;
constexpr char CAROUSEL_CACHE_PATH[] = "/.crosspoint/home_carousel_cache.bin";
constexpr char CAROUSEL_CACHE_TMP_PATH[] = "/.crosspoint/home_carousel_cache.tmp";

struct CarouselCacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t frameCount;
  uint32_t frameBufferSize;
  uint64_t keyHash;
  uint16_t screenWidth;
  uint16_t screenHeight;
  uint16_t centerCoverW;
  uint16_t centerCoverH;
  uint16_t sideCoverW;
  uint16_t sideCoverH;
};

uint64_t fnvHash64(const std::string& s) {
  uint64_t hash = 14695981039346656037ull;
  for (char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

void appendHashedFileStateToKey(std::string& key, const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("HOME", path, file)) {
    key += "missing";
    key += '\0';
    return;
  }

  uint64_t hash = 14695981039346656037ull;
  size_t totalBytes = 0;
  uint8_t buffer[64];
  while (true) {
    const int bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;
    totalBytes += static_cast<size_t>(bytesRead);
    for (int i = 0; i < bytesRead; ++i) {
      hash ^= buffer[i];
      hash *= 1099511628211ull;
    }
  }
  file.close();

  char digest[48];
  snprintf(digest, sizeof(digest), "%zu:%" PRIu64, totalBytes, static_cast<uint64_t>(hash));
  key += digest;
  key += '\0';
}

std::string getRecentBookCachePath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return "/.crosspoint/xtc_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  return "";
}

void appendCarouselCoverStateToKey(std::string& key, const RecentBook& book) {
  key += book.path;
  key += '\0';
  key += book.coverBmpPath;
  key += '\0';

  if (book.coverBmpPath.empty()) {
    key += "0:0";
    key += '\0';
    return;
  }

  const std::string centerPath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH);
  const std::string sidePath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH);
  key += Storage.exists(centerPath.c_str()) ? '1' : '0';
  key += ':';
  key += Storage.exists(sidePath.c_str()) ? '1' : '0';
  key += '\0';

  const std::string cachePath = getRecentBookCachePath(book);
  if (!cachePath.empty()) {
    appendHashedFileStateToKey(key, cachePath + "/progress.bin");
  } else {
    key += "no-cache-path";
    key += '\0';
  }
}

void buildCarouselCacheKey(const std::vector<RecentBook>& recentBooks, std::string& key, uint64_t& keyHash) {
  key.clear();
  key.reserve(512);
  for (const auto& book : recentBooks) {
    appendCarouselCoverStateToKey(key, book);
  }
  keyHash = fnvHash64(key);
}

bool isCarouselCacheHeaderValid(const CarouselCacheHeader& header, uint64_t cacheKeyHash, int bookCount,
                                const GfxRenderer& renderer) {
  return header.magic == CAROUSEL_CACHE_MAGIC && header.version == CAROUSEL_CACHE_VERSION &&
         header.keyHash == cacheKeyHash && header.frameCount == bookCount &&
         header.frameBufferSize == renderer.getBufferSize() && header.screenWidth == renderer.getScreenWidth() &&
         header.screenHeight == renderer.getScreenHeight() && header.centerCoverW == LyraCarouselTheme::kCenterCoverW &&
         header.centerCoverH == LyraCarouselTheme::kCenterCoverH &&
         header.sideCoverW == LyraCarouselTheme::kSideCoverW && header.sideCoverH == LyraCarouselTheme::kSideCoverH;
}

bool readCarouselCacheHeader(FsFile& file, CarouselCacheHeader& header) {
  CarouselCacheHeader readHeader{};
  if (!serialization::readPod(file, readHeader)) return false;
  header = readHeader;
  return true;
}

bool hasValidCarouselDiskCache(const std::vector<RecentBook>& recentBooks, const GfxRenderer& renderer) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount <= 0) return false;

  std::string cacheKey;
  uint64_t cacheKeyHash = 0;
  buildCarouselCacheKey(recentBooks, cacheKey, cacheKeyHash);

  FsFile cacheFile;
  if (!Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, cacheFile)) return false;

  CarouselCacheHeader header{};
  const bool readOk = readCarouselCacheHeader(cacheFile, header);
  cacheFile.close();
  return readOk && isCarouselCacheHeaderValid(header, cacheKeyHash, bookCount, renderer);
}

// ---------------------------------------------------------------------------
// Static carousel frame cache — survives HomeActivity re-creation so that
// returning to home (e.g. after settings) doesn't re-read covers from SD.
// Freed explicitly in onSelectBook / openSelectedBook() before entering reader.
// ---------------------------------------------------------------------------
class CarouselCache {
 public:
  uint8_t* frames[HomeActivity::kCarouselFrameCount] = {};
  int frameBookIdx[HomeActivity::kCarouselFrameCount] = {-1};
  int frameCount = 0;
  int lastCenterIdx = -1;
  std::string key;
  uint64_t keyHash = 0;

  int findFrameSlot(int bookIdx) const {
    for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
      if (frameBookIdx[i] == bookIdx && frames[i] != nullptr) return i;
    }
    return -1;
  }

  void invalidate() {
    for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
      if (frames[i]) {
        free(frames[i]);
        frames[i] = nullptr;
      }
      frameBookIdx[i] = -1;
    }
    frameCount = 0;
    lastCenterIdx = -1;
    key.clear();
    keyHash = 0;
  }
};

CarouselCache gCarouselCache;
}  // namespace

static_assert(HomeActivity::kMaxCachedBooks >= LyraCarouselMetrics::values.homeRecentBooksCount,
              "kMaxCachedBooks must cover all carousel slots");

// Static cover cache state
bool HomeActivity::coverRendered = false;
bool HomeActivity::coverBufferStored = false;
uint8_t* HomeActivity::coverBuffer = nullptr;
std::vector<std::string> HomeActivity::coverCacheBookPaths;

int HomeActivity::getMenuItemCount() const {
  int count = 3;  // My Library, File transfer, Settings
  if (hasContinueReading) count++;
  if (core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl})) count++;
  if (core::HomeActionRegistry::shouldExpose("todo_planner", {false})) count++;
  if (core::HomeActionRegistry::shouldExpose("anki", {false})) count++;
  return count;
}

std::string HomeActivity::fallbackTitleFromPath(const std::string& path) {
  auto title = path;
  const size_t lastSlash = title.find_last_of('/');
  if (lastSlash != std::string::npos) {
    title = title.substr(lastSlash + 1);
  }

  if (FsHelpers::checkFileExtension(title, ".xtch")) {
    title.resize(title.length() - 5);
  } else if (FsHelpers::checkFileExtension(title, ".epub") || FsHelpers::checkFileExtension(title, ".xtc") ||
             FsHelpers::checkFileExtension(title, ".txt") || FsHelpers::checkFileExtension(title, ".md")) {
    title.resize(title.length() - 4);
  }

  return title;
}

std::string HomeActivity::fallbackAuthor(const RecentBook& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return "";
}

bool HomeActivity::isPokemonPartyHomeMode() const {
  return SETTINGS.uiTheme == CrossPointSettings::FORK_DRIFT &&
         core::FeatureModules::hasCapability(core::Capability::PokemonParty);
}

void HomeActivity::rebuildMenuLayout() {
  const bool forkDrift = SETTINGS.uiTheme == CrossPointSettings::FORK_DRIFT;
  if (forkDrift) {
    if (isPokemonPartyHomeMode()) {
      menuOpenBookIndex = -1;
      menuMyLibraryIndex = -1;
      menuOpdsIndex = -1;
      menuTodoIndex = -1;
      menuAnkiIndex = -1;
      menuFileTransferIndex = -1;
      menuSettingsIndex = 0;
      menuItemCount = 1;
      return;
    }
    menuOpenBookIndex = -1;
    int idx = 0;
    menuMyLibraryIndex = idx++;
    menuOpdsIndex = -1;
    menuTodoIndex = core::HomeActionRegistry::shouldExpose("todo_planner", {false}) ? idx++ : -1;
    menuAnkiIndex = core::HomeActionRegistry::shouldExpose("anki", {false}) ? idx++ : -1;
    menuFileTransferIndex = idx++;
    menuSettingsIndex = idx++;
    menuItemCount = idx;
    return;
  }
  int idx = 0;
  menuOpenBookIndex = idx++;
  menuMyLibraryIndex = idx++;
  menuOpdsIndex = core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl}) ? idx++ : -1;
  menuTodoIndex = core::HomeActionRegistry::shouldExpose("todo_planner", {false}) ? idx++ : -1;
  menuAnkiIndex = core::HomeActionRegistry::shouldExpose("anki", {false}) ? idx++ : -1;
  menuFileTransferIndex = idx++;
  menuSettingsIndex = idx++;
  menuItemCount = idx;
}

void HomeActivity::loadRecentBooks() {
  auto metrics = UITheme::getInstance().getMetrics();
  const int maxBooks = metrics.homeRecentBooksCount;

  recentBooks.clear();
  const auto& storedBooks = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<size_t>(maxBooks), storedBooks.size()));

  for (const auto& stored : storedBooks) {
    if (recentBooks.size() >= static_cast<size_t>(maxBooks)) {
      break;
    }

    RecentBook entry = stored;
    if (entry.title.empty()) {
      entry.title = fallbackTitleFromPath(entry.path);
      if (entry.title != stored.title) {
        RECENT_BOOKS.updateBook(entry.path, entry.title, entry.author, entry.coverBmpPath);
      }
    }
    recentBooks.push_back(entry);
  }

  if (recentBooks.empty()) {
    selectedBookIndex = 0;
    return;
  }

  if (!APP_STATE.openEpubPath.empty()) {
    for (size_t i = 0; i < recentBooks.size(); ++i) {
      if (recentBooks[i].path == APP_STATE.openEpubPath) {
        selectedBookIndex = static_cast<int>(i);
        return;
      }
    }
  }

  selectedBookIndex = std::min(selectedBookIndex, static_cast<int>(recentBooks.size()) - 1);
  selectedBookIndex = std::max(0, selectedBookIndex);
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  SpiBusMutex::Guard guard;
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const size_t recentBookCount = recentBooks.size();
  std::vector<char> bookUpdated(recentBookCount, false);
  const int progressIncrement = 90 / static_cast<int>(std::max<size_t>(1, recentBookCount));

  int progress = 0;
  for (size_t bookIdx = 0; bookIdx < recentBooks.size(); ++bookIdx) {
    RecentBook& book = recentBooks[bookIdx];
    if (!book.coverBmpPath.empty()) {
      if (isCarouselTheme) {
        const std::string centerPath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterCoverW,
                                                                  LyraCarouselTheme::kCenterCoverH);
        const std::string sidePath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kSideCoverW,
                                                                LyraCarouselTheme::kSideCoverH);
        const bool centerMissing = !Storage.exists(centerPath.c_str());
        const bool sideMissing = !Storage.exists(sidePath.c_str());

        if (centerMissing || sideMissing) {
          if (FsHelpers::hasEpubExtension(book.path)) {
            Epub epub(book.path, "/.crosspoint");
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
            if (!epub.load(false, true)) {
              LOG_ERR("HOME", "carousel: failed to load EPUB for thumb: %s", book.path.c_str());
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
              coverRendered = false;
              requestUpdate();
              progress++;
              continue;
            }
            bool success = true;
            if (centerMissing)
              success =
                  epub.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) && success;
            if (sideMissing)
              success =
                  epub.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            } else {
              bookUpdated[bookIdx] = true;
            }
            coverRendered = false;
            requestUpdate();
          } else if (FsHelpers::hasXtcExtension(book.path)) {
            Xtc xtc(book.path, "/.crosspoint");
            if (xtc.load()) {
              if (!showingLoading) {
                showingLoading = true;
                popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
              }
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
              bool success = true;
              if (centerMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) && success;
              if (sideMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
              if (!success) {
                RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
                book.coverBmpPath = "";
              } else {
                bookUpdated[bookIdx] = true;
              }
              coverRendered = false;
              requestUpdate();
            }
          }
        }
      } else {
        std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
        if (!Storage.exists(coverPath.c_str())) {
          const auto homeCardData = core::FeatureModules::resolveHomeCardData(book.path, coverHeight);
          if (homeCardData.handled) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);

            if (!homeCardData.coverPath.empty()) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, homeCardData.coverPath);
              book.coverBmpPath = homeCardData.coverPath;
            } else if (homeCardData.loaded) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;

  if (isCarouselTheme) {
    bool anyUpdated = false;
    for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
      if (static_cast<size_t>(i) >= bookUpdated.size() || !bookUpdated[i]) continue;
      anyUpdated = true;
      if (carouselFramesReady) {
        const int slot = gCarouselCache.findFrameSlot(i);
        if (slot >= 0) renderCarouselFrame(i, slot);
      }
    }
    if (anyUpdated) {
      if (!carouselFramesReady) {
        if (Storage.exists(CAROUSEL_CACHE_PATH)) Storage.remove(CAROUSEL_CACHE_PATH);
        if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) Storage.remove(CAROUSEL_CACHE_TMP_PATH);
        preRenderCarouselFrames();
      } else {
        if (Storage.exists(CAROUSEL_CACHE_PATH)) Storage.remove(CAROUSEL_CACHE_PATH);
        if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) Storage.remove(CAROUSEL_CACHE_TMP_PATH);
      }
      requestUpdate();
    }
  }
}

void HomeActivity::openSelectedBook() {
  if (recentBooks.empty()) {
    return;
  }

  if (selectedBookIndex < 0 || selectedBookIndex >= static_cast<int>(recentBooks.size())) {
    selectedBookIndex = 0;
  }

  const auto& selected = recentBooks[static_cast<size_t>(selectedBookIndex)];
  if (!Storage.exists(selected.path.c_str())) {
    loadRecentBooks();
    requestUpdate();
    return;
  }

  gCarouselCache.invalidate();
  freeCarouselFrames();
  APP_STATE.openEpubPath = selected.path;
  APP_STATE.saveToFile();
  onContinueReading();
}

std::string HomeActivity::getMenuItemLabel(const int index) const {
  if (isPokemonPartyHomeMode()) {
    if (index == menuSettingsIndex) {
      return "Settings";
    }
    return "";
  }
  if (index == menuOpenBookIndex) {
    return recentBooks.empty() ? "Open Book (empty)" : "Open Book";
  }
  if (index == menuMyLibraryIndex) {
    return "My Library";
  }
  if (index == menuOpdsIndex) {
    return "OPDS Browser";
  }
  if (index == menuTodoIndex) {
    return "TODO";
  }
  if (index == menuAnkiIndex) {
    return "Anki";
  }
  if (index == menuFileTransferIndex) {
    return "File Transfer";
  }
  if (index == menuSettingsIndex) {
    return "Settings";
  }
  return "";
}

bool HomeActivity::drawCoverAt(const std::string& coverPath, const int x, const int y, const int width,
                               const int height) const {
  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    return false;
  }

  SpiBusMutex::Guard guard;
  FsFile file;
  if (!Storage.openFileForRead("HOME", coverPath, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    renderer.drawBitmap(bitmap, x, y, width, height);
  }
  file.close();
  return ok;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;
  const bool mediaPickerEnabled = core::FeatureModules::hasCapability(core::Capability::HomeMediaPicker);
  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  carouselFramesReady = false;
  carouselWarmupPending = isCarouselTheme;

  if (mediaPickerEnabled) {
    loadRecentBooks();
    currentBookProgressPercent = -1.0f;
    if (!recentBooks.empty()) {
      BookProgressDataStore::ProgressData progress{};
      if (BookProgressDataStore::loadProgress(recentBooks[0].path, progress)) {
        currentBookProgressPercent = progress.percent;
      }
    }

    // Reset selector; restore last carousel book position when re-entering
    selectorIndex = 0;
    lastCarouselBookIndex = 0;
    if (isCarouselTheme && !APP_STATE.openEpubPath.empty()) {
      for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
        if (recentBooks[i].path == APP_STATE.openEpubPath) {
          selectorIndex = i;
          lastCarouselBookIndex = i;
          break;
        }
      }
    }

    if (isCarouselTheme) {
      loadBookProgress();
    }

    rebuildMenuLayout();
    selectedMenuIndex = 0;
    inButtonGrid = (SETTINGS.uiTheme == CrossPointSettings::FORK_DRIFT && recentBooks.empty());
    hasContinueReading = !recentBooks.empty();

    hasCoverImage = false;
    coverBmpPath.clear();
    lastBookTitle.clear();
    lastBookAuthor.clear();

    // Invalidate cached cover buffer only when the recent book list has changed.
    // If the same books are shown, restoreCoverBuffer() will reuse the static
    // buffer and skip the slow SD card BMP reload entirely.
    // Also skip loadRecentCovers() — thumbnails were already verified last visit.
    if (isCoverCacheValid()) {
      recentsLoaded = true;
    } else {
      freeCoverBuffer();
      coverRendered = false;
    }

    if (isCarouselTheme && hasValidCarouselDiskCache(recentBooks, renderer)) {
      preRenderCarouselFrames(false);
    }
  } else {
    // Check if we have a book to continue reading
    hasContinueReading = !APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str());

    if (hasContinueReading) {
      // Extract filename from path for display
      lastBookTitle = APP_STATE.openEpubPath;
      const size_t lastSlash = lastBookTitle.find_last_of('/');
      if (lastSlash != std::string::npos) {
        lastBookTitle = lastBookTitle.substr(lastSlash + 1);
      }

      const int thumbHeight = renderer.getScreenHeight() / 2;
      const auto homeCardData = core::FeatureModules::resolveHomeCardData(APP_STATE.openEpubPath, thumbHeight);
      if (!homeCardData.title.empty()) {
        lastBookTitle = homeCardData.title;
      }
      if (!homeCardData.author.empty()) {
        lastBookAuthor = homeCardData.author;
      }
      if (!homeCardData.coverPath.empty()) {
        coverBmpPath = homeCardData.coverPath;
        hasCoverImage = true;
      }

      // Preserve previous xtc fallback behavior when metadata is unavailable.
      if (homeCardData.handled && homeCardData.title.empty()) {
        if (FsHelpers::checkFileExtension(lastBookTitle, ".xtch")) {
          lastBookTitle.resize(lastBookTitle.length() - 5);
        } else if (FsHelpers::checkFileExtension(lastBookTitle, ".xtc")) {
          lastBookTitle.resize(lastBookTitle.length() - 4);
        }
      }
    }

    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Do NOT free coverBuffer here — it is static and persists so the next home
  // visit can restore covers instantly without reloading from SD card.
  gCarouselCache.invalidate();
  freeCarouselFrames();
  carouselWarmupPending = false;
  recentBooks.clear();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  coverBufferStored = true;

  // Record which books' covers are now in the buffer so we can validate on re-entry.
  coverCacheBookPaths.resize(recentBooks.size());
  std::transform(recentBooks.begin(), recentBooks.end(), coverCacheBookPaths.begin(),
                 [](const RecentBook& book) { return book.path; });

  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
  coverCacheBookPaths.clear();
}

bool HomeActivity::isCoverCacheValid() const {
  if (!coverBufferStored || !coverBuffer) {
    return false;
  }
  if (coverCacheBookPaths.size() != recentBooks.size()) {
    return false;
  }
  for (size_t i = 0; i < coverCacheBookPaths.size(); i++) {
    if (coverCacheBookPaths[i] != recentBooks[i].path) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Carousel frame cache
// ---------------------------------------------------------------------------

void HomeActivity::loadBookProgress() {
  const int count = std::min(static_cast<int>(recentBooks.size()), kMaxCachedBooks);
  for (int i = 0; i < count; ++i) {
    BookProgressDataStore::ProgressData pd{};
    cachedBookProgress[i] = BookProgressDataStore::loadProgress(recentBooks[i].path, pd) ? pd.percent : -1.0f;
  }
  bookProgressCached = true;
}

void HomeActivity::freeCarouselFrames() {
  for (int i = 0; i < kCarouselFrameCount; ++i) carouselFrames[i] = nullptr;
  carouselFramesReady = false;
}

bool HomeActivity::allocateCarouselFrameSlots(int targetFrameCount) {
  const size_t bufferSize = renderer.getBufferSize();
  int frameCount = 0;
  for (int attempt = targetFrameCount; attempt >= 1; --attempt) {
    bool allocFailed = false;
    for (int i = 0; i < attempt; ++i) {
      gCarouselCache.frames[i] = static_cast<uint8_t*>(malloc(bufferSize));
      if (!gCarouselCache.frames[i]) {
        LOG_ERR("HOME", "carousel: malloc failed for frame %d (attempt %d)", i, attempt);
        allocFailed = true;
        break;
      }
      gCarouselCache.frameBookIdx[i] = -1;
    }
    if (!allocFailed) {
      frameCount = attempt;
      break;
    }
    for (int i = 0; i < attempt; ++i) {
      if (gCarouselCache.frames[i]) {
        free(gCarouselCache.frames[i]);
        gCarouselCache.frames[i] = nullptr;
      }
      gCarouselCache.frameBookIdx[i] = -1;
    }
  }
  if (frameCount == 0) {
    gCarouselCache.invalidate();
    return false;
  }
  gCarouselCache.frameCount = frameCount;
  LOG_INF("HOME", "carousel: frame cache capacity %d/%d", frameCount, targetFrameCount);
  return true;
}

void HomeActivity::renderCarouselFrameToCurrentBuffer(int bookIdx, float* outProgressPercent) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bookCount = static_cast<int>(recentBooks.size());
  bool dummy1 = false, dummy2 = false, dummy3 = false;
  float frameProgressPercent = -1.0f;

  if (bookIdx >= 0 && bookIdx < bookCount) {
    if (bookProgressCached && bookIdx < kMaxCachedBooks) {
      frameProgressPercent = cachedBookProgress[bookIdx];
    } else {
      BookProgressDataStore::ProgressData pd{};
      if (BookProgressDataStore::loadProgress(recentBooks[bookIdx].path, pd)) {
        frameProgressPercent = pd.percent;
      }
    }
  }

  LyraCarouselTheme::setPreRenderIndex(bookIdx);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  // selectorIndex == bookCount means "in menu row" — no selection highlight on the cover strip
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, bookCount, dummy1, dummy2, dummy3, []() { return true; },
                          frameProgressPercent);

  std::vector<std::string> menuLabels;
  std::vector<UIIcon> menuIcons;
  menuLabels.reserve(4);
  menuIcons.reserve(4);
  menuLabels.push_back("Open Book");
  menuIcons.push_back(Book);
  menuLabels.push_back("My Library");
  menuIcons.push_back(Folder);
  if (core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl})) {
    menuLabels.push_back("OPDS Browser");
    menuIcons.push_back(Library);
  }
  menuLabels.push_back("File Transfer");
  menuIcons.push_back(Transfer);
  menuLabels.push_back("Settings");
  menuIcons.push_back(Settings);

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing * 2 +
                         metrics.buttonHintsHeight)},
      static_cast<int>(menuLabels.size()), -1, [&menuLabels](int index) { return menuLabels[index]; },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (outProgressPercent) *outProgressPercent = frameProgressPercent;
}

bool HomeActivity::buildCarouselCacheFile(const std::string& cacheKey, uint64_t cacheKeyHash, int bookCount,
                                          bool showProgressPopup) {
  (void)cacheKey;
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || bookCount <= 0) return false;

  Storage.mkdir("/.crosspoint");
  if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) Storage.remove(CAROUSEL_CACHE_TMP_PATH);

  FsFile file;
  if (!Storage.openFileForWrite("HOME", CAROUSEL_CACHE_TMP_PATH, file)) return false;

  const CarouselCacheHeader header = {
      CAROUSEL_CACHE_MAGIC,
      CAROUSEL_CACHE_VERSION,
      static_cast<uint16_t>(bookCount),
      static_cast<uint32_t>(renderer.getBufferSize()),
      cacheKeyHash,
      static_cast<uint16_t>(renderer.getScreenWidth()),
      static_cast<uint16_t>(renderer.getScreenHeight()),
      static_cast<uint16_t>(LyraCarouselTheme::kCenterCoverW),
      static_cast<uint16_t>(LyraCarouselTheme::kCenterCoverH),
      static_cast<uint16_t>(LyraCarouselTheme::kSideCoverW),
      static_cast<uint16_t>(LyraCarouselTheme::kSideCoverH),
  };
  serialization::writePod(file, header);

  const size_t bufferSize = renderer.getBufferSize();
  Rect popupRect{};
  if (showProgressPopup) {
    popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    GUI.fillPopupProgress(renderer, popupRect, 0);
  }

  const auto start = millis();
  bool writeFailed = false;
  for (int i = 0; i < bookCount; ++i) {
    const int cachedSlot = gCarouselCache.findFrameSlot(i);
    if (cachedSlot >= 0 && carouselFrames[cachedSlot]) {
      memcpy(frameBuffer, carouselFrames[cachedSlot], bufferSize);
    } else {
      for (int slot = 0; slot < kCarouselFrameCount; ++slot) {
        if (gCarouselCache.frames[slot]) {
          free(gCarouselCache.frames[slot]);
          gCarouselCache.frames[slot] = nullptr;
        }
        gCarouselCache.frameBookIdx[slot] = -1;
        carouselFrames[slot] = nullptr;
      }
      renderCarouselFrameToCurrentBuffer(i, nullptr);
    }
    if (file.write(frameBuffer, bufferSize) != bufferSize) {
      writeFailed = true;
      break;
    }
    if (showProgressPopup) {
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, ((i + 1) * 100) / bookCount);
    }
  }

  file.flush();
  file.close();

  if (writeFailed) {
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to write cache snapshot");
    return false;
  }
  if (Storage.exists(CAROUSEL_CACHE_PATH)) Storage.remove(CAROUSEL_CACHE_PATH);
  if (!Storage.rename(CAROUSEL_CACHE_TMP_PATH, CAROUSEL_CACHE_PATH)) {
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to promote cache snapshot");
    return false;
  }

  LOG_DBG("HOME", "carousel: built cache for %d book(s) in %lums", bookCount, millis() - start);
  return true;
}

bool HomeActivity::loadCarouselFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx) {
  if (slotIdx < 0 || slotIdx >= kCarouselFrameCount || bookIdx < 0 || bookIdx >= bookCount) {
    return false;
  }
  if (!gCarouselCache.frames[slotIdx]) {
    gCarouselCache.frames[slotIdx] = static_cast<uint8_t*>(malloc(renderer.getBufferSize()));
    if (!gCarouselCache.frames[slotIdx]) {
      LOG_ERR("HOME", "carousel: malloc failed for disk slot %d", slotIdx);
      return false;
    }
  }
  FsFile file;
  if (!Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, file)) return false;

  CarouselCacheHeader header{};
  if (!readCarouselCacheHeader(file, header) ||
      !isCarouselCacheHeaderValid(header, cacheKeyHash, bookCount, renderer)) {
    file.close();
    return false;
  }

  const size_t frameOffset = sizeof(CarouselCacheHeader) + static_cast<size_t>(bookIdx) * renderer.getBufferSize();
  if (!file.seek(frameOffset)) {
    file.close();
    return false;
  }

  const size_t expectedBytes = renderer.getBufferSize();
  size_t totalRead = 0;
  while (totalRead < expectedBytes) {
    const int n = file.read(gCarouselCache.frames[slotIdx] + totalRead, expectedBytes - totalRead);
    if (n <= 0) break;
    totalRead += static_cast<size_t>(n);
  }
  file.close();

  if (totalRead != expectedBytes) {
    LOG_ERR("HOME", "carousel: short read slot %d (%zu/%zu bytes)", slotIdx, totalRead, expectedBytes);
    return false;
  }
  gCarouselCache.frameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCarouselCache.frames[slotIdx];
  return true;
}

int HomeActivity::chooseCarouselEvictionSlot(int centerIdx, int bookCount,
                                             std::optional<int> protectedBookIdx) const {
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (gCarouselCache.frameBookIdx[i] < 0) return i;
  }
  int evictSlot = -1;
  int maxDist = -1;
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (!gCarouselCache.frames[i]) continue;
    const int cachedBookIdx = gCarouselCache.frameBookIdx[i];
    if (protectedBookIdx.has_value() && cachedBookIdx == protectedBookIdx.value()) continue;
    const int diff = std::abs(cachedBookIdx - centerIdx);
    const int dist = std::min(diff, bookCount - diff);
    if (dist > maxDist) {
      maxDist = dist;
      evictSlot = i;
    }
  }
  return evictSlot;
}

void HomeActivity::renderCarouselFrame(int bookIdx, int slotIdx) {
  const auto start = millis();
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || slotIdx < 0 || slotIdx >= kCarouselFrameCount) return;

  // Free the destination cache slot while drawing. Carousel frames can be a full
  // framebuffer each; holding one during grayscale rendering can starve the BW
  // backup chunks and abort the render path on low-heap devices.
  if (gCarouselCache.frames[slotIdx]) {
    free(gCarouselCache.frames[slotIdx]);
    gCarouselCache.frames[slotIdx] = nullptr;
    gCarouselCache.frameBookIdx[slotIdx] = -1;
    carouselFrames[slotIdx] = nullptr;
  }

  float progressPercent = -1.0f;
  renderCarouselFrameToCurrentBuffer(bookIdx, &progressPercent);

  gCarouselCache.frames[slotIdx] = static_cast<uint8_t*>(malloc(renderer.getBufferSize()));
  if (!gCarouselCache.frames[slotIdx]) {
    LOG_ERR("HOME", "carousel: malloc failed for rendered slot %d", slotIdx);
    return;
  }
  memcpy(gCarouselCache.frames[slotIdx], frameBuffer, renderer.getBufferSize());
  gCarouselCache.frameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCarouselCache.frames[slotIdx];
  LOG_DBG("HOME", "carousel: renderCarouselFrame book=%d slot=%d took %lums", bookIdx, slotIdx, millis() - start);
}

void HomeActivity::updateSlidingWindowCache(int centerIdx, int bookCount) {
  (void)centerIdx;
  (void)bookCount;
  // One-frame cache: adjacent frames paged from SD snapshot on demand in render().
}

bool HomeActivity::preRenderCarouselFrames(bool showProgressPopup) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount == 0) return false;
  bool showedProgressPopup = false;

  std::string newKey;
  uint64_t newKeyHash = 0;
  buildCarouselCacheKey(recentBooks, newKey, newKeyHash);

  if (newKey == gCarouselCache.key && gCarouselCache.frameCount > 0) {
    for (int i = 0; i < gCarouselCache.frameCount; ++i) carouselFrames[i] = gCarouselCache.frames[i];
    carouselFramesReady = true;
    coverRendered = false;
    coverBufferStored = false;
    return false;
  }

  if (!renderer.getFrameBuffer()) return false;
  freeCoverBuffer();
  gCarouselCache.invalidate();

  const int targetFrameCount = std::min(bookCount, kCarouselFrameCount);
  bool diskCacheValid = false;
  {
    FsFile cacheFile;
    if (Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, cacheFile)) {
      CarouselCacheHeader header{};
      const bool readOk = readCarouselCacheHeader(cacheFile, header);
      cacheFile.close();
      diskCacheValid = readOk && isCarouselCacheHeaderValid(header, newKeyHash, bookCount, renderer);
    }
  }

  if (!allocateCarouselFrameSlots(targetFrameCount)) return showedProgressPopup;

  const int selectedBookIdx = (selectorIndex < bookCount) ? selectorIndex : lastCarouselBookIndex;
  const int initialBookIdx = (selectedBookIdx >= 0 && selectedBookIdx < bookCount) ? selectedBookIdx : 0;

  auto loadOrRender = [&](int bookIdx, int slot) {
    if (!diskCacheValid || !loadCarouselFrameFromDisk(newKeyHash, bookCount, bookIdx, slot)) {
      renderCarouselFrame(bookIdx, slot);
    }
  };
  loadOrRender(initialBookIdx, 0);
  gCarouselCache.lastCenterIdx = initialBookIdx;

  const bool hasFullFrameCache = gCarouselCache.frameCount >= targetFrameCount;
  gCarouselCache.key = newKey;
  gCarouselCache.keyHash = diskCacheValid ? newKeyHash : 0;
  carouselFramesReady = true;
  coverRendered = false;
  coverBufferStored = false;

  if (!diskCacheValid && gCarouselCache.frameCount > 0 && hasFullFrameCache) {
    const bool cacheBuilt = buildCarouselCacheFile(newKey, newKeyHash, bookCount, showProgressPopup);
    if (cacheBuilt) {
      gCarouselCache.keyHash = newKeyHash;
      showedProgressPopup = true;
    }
  }
  return showedProgressPopup;
}

void HomeActivity::loop() {
  const bool mediaPickerEnabled = core::FeatureModules::hasCapability(core::Capability::HomeMediaPicker);

  if (mediaPickerEnabled) {
    const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
    const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
    const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
    const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
    const bool forkDrift = SETTINGS.uiTheme == CrossPointSettings::FORK_DRIFT;
    const bool pokemonPartyHomeMode = isPokemonPartyHomeMode();

    if (pokemonPartyHomeMode && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goToRecentBooks();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (forkDrift) {
        if (inButtonGrid) {
          if (selectedMenuIndex == menuSettingsIndex) {
            onSettingsOpen();
            return;
          }
          if (!pokemonPartyHomeMode) {
            if (selectedMenuIndex == menuMyLibraryIndex) {
              onMyLibraryOpen();
              return;
            }
            if (selectedMenuIndex == menuTodoIndex) {
              onTodoOpen();
              return;
            }
            if (selectedMenuIndex == menuAnkiIndex) {
              onAnkiOpen();
              return;
            }
            if (selectedMenuIndex == menuFileTransferIndex) {
              onFileTransferOpen();
              return;
            }
          }
        } else if (!recentBooks.empty()) {
          openSelectedBook();
          return;
        }
      } else {
        if (selectedMenuIndex == menuOpenBookIndex) {
          openSelectedBook();
          return;
        }
        if (selectedMenuIndex == menuMyLibraryIndex) {
          onMyLibraryOpen();
          return;
        }
        if (selectedMenuIndex == menuOpdsIndex) {
          onOpdsBrowserOpen();
          return;
        }
        if (selectedMenuIndex == menuTodoIndex) {
          onTodoOpen();
          return;
        }
        if (selectedMenuIndex == menuAnkiIndex) {
          onAnkiOpen();
          return;
        }
        if (selectedMenuIndex == menuFileTransferIndex) {
          onFileTransferOpen();
          return;
        }
        if (selectedMenuIndex == menuSettingsIndex) {
          onSettingsOpen();
          return;
        }
      }
    }

    const bool isCarouselTheme =
        static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

    if (isCarouselTheme && !recentBooks.empty()) {
      const int bookCount = static_cast<int>(recentBooks.size());
      const int menuCount = getMenuItemCount();
      const bool inCarouselRow = (selectorIndex < bookCount);

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        if (inCarouselRow) {
          selectedBookIndex = selectorIndex;
          openSelectedBook();
          return;
        }
        // In menu row — route by offset from bookCount
        const int menuIdx = selectorIndex - bookCount;
        // Reconstruct menu order: Open Book, My Library, [OPDS], File Transfer, Settings
        int midx = 0;
        const int openBookIdx = midx++;
        const int myLibraryIdx = midx++;
        const int opdsIdx = core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl}) ? midx++ : -1;
        const int fileTransferIdx = midx++;
        const int settingsIdx = midx;
        if (menuIdx == openBookIdx) {
          selectedBookIndex = lastCarouselBookIndex;
          openSelectedBook();
        } else if (menuIdx == myLibraryIdx) {
          onMyLibraryOpen();
        } else if (menuIdx == opdsIdx) {
          onOpdsBrowserOpen();
        } else if (menuIdx == fileTransferIdx) {
          onFileTransferOpen();
        } else if (menuIdx == settingsIdx) {
          onSettingsOpen();
        }
        return;
      }

      if (leftPressed) {
        if (inCarouselRow) {
          selectorIndex = (selectorIndex + bookCount - 1) % bookCount;
          lastCarouselBookIndex = selectorIndex;
        } else {
          const int menuOffset = selectorIndex - bookCount;
          selectorIndex = bookCount + (menuOffset + menuCount - 1) % menuCount;
        }
        requestUpdate();
      } else if (rightPressed) {
        if (inCarouselRow) {
          selectorIndex = (selectorIndex + 1) % bookCount;
          lastCarouselBookIndex = selectorIndex;
        } else {
          const int menuOffset = selectorIndex - bookCount;
          selectorIndex = bookCount + (menuOffset + 1) % menuCount;
        }
        requestUpdate();
      } else if (downPressed) {
        if (inCarouselRow) {
          selectorIndex = bookCount;  // enter menu row
          requestUpdate();
        }
      } else if (upPressed) {
        if (!inCarouselRow) {
          selectorIndex = lastCarouselBookIndex;  // back to carousel row
          requestUpdate();
        }
      }
      return;
    }

    if (forkDrift) {
      constexpr int coverCols = 3;
      const int bookCount = static_cast<int>(recentBooks.size());
      const int coverRows = bookCount > 3 ? 2 : 1;

      if (inButtonGrid) {
        if (upPressed) {
          if (selectedMenuIndex == 0 && bookCount > 0) {
            inButtonGrid = false;
            selectedBookIndex = std::min(selectedBookIndex, bookCount - 1);
            selectedBookIndex = std::max(0, selectedBookIndex);
            requestUpdate();
          } else if (!pokemonPartyHomeMode && selectedMenuIndex > 0) {
            selectedMenuIndex--;
            requestUpdate();
          }
        } else if (!pokemonPartyHomeMode && downPressed) {
          if (selectedMenuIndex < menuItemCount - 1) {
            selectedMenuIndex++;
            requestUpdate();
          }
        }
      } else {
        if (bookCount > 0 && (leftPressed || rightPressed || upPressed || downPressed)) {
          const auto nav = ForkDriftNavigation::navigateCoverGrid(selectedBookIndex, bookCount, coverCols, coverRows,
                                                                  leftPressed, rightPressed, upPressed, downPressed);
          if (nav.enterButtonGrid) {
            inButtonGrid = true;
            selectedMenuIndex = 0;
          } else {
            selectedBookIndex = nav.bookIndex;
          }
          requestUpdate();
        }
      }
    } else {
      if (!recentBooks.empty()) {
        const int bookCount = static_cast<int>(recentBooks.size());
        if (leftPressed) {
          selectedBookIndex = (selectedBookIndex + bookCount - 1) % bookCount;
          requestUpdate();
        } else if (rightPressed) {
          selectedBookIndex = (selectedBookIndex + 1) % bookCount;
          requestUpdate();
        }
      }

      if (menuItemCount > 0) {
        if (upPressed) {
          selectedMenuIndex = (selectedMenuIndex + menuItemCount - 1) % menuItemCount;
          requestUpdate();
        } else if (downPressed) {
          selectedMenuIndex = (selectedMenuIndex + 1) % menuItemCount;
          requestUpdate();
        }
      }
    }
    return;
  }

  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    const int continueIdx = hasContinueReading ? idx++ : -1;
    const int myLibraryIdx = idx++;
    const int opdsLibraryIdx = core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl}) ? idx++ : -1;
    const int todoIdx = core::HomeActionRegistry::shouldExpose("todo_planner", {false}) ? idx++ : -1;
    const int ankiIdx = core::HomeActionRegistry::shouldExpose("anki", {false}) ? idx++ : -1;
    const int notesIdx = idx++;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex == continueIdx) {
      onContinueReading();
    } else if (selectorIndex == myLibraryIdx) {
      onMyLibraryOpen();
    } else if (selectorIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
    } else if (selectorIndex == todoIdx) {
      onTodoOpen();
    } else if (selectorIndex == ankiIdx) {
      onAnkiOpen();
    } else if (selectorIndex == notesIdx) {
      onNotesOpen();
    } else if (selectorIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (selectorIndex == settingsIdx) {
      onSettingsOpen();
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int topInset = features::status_overlay::topInset();
  const int usablePageHeight = pageHeight - topInset;

  // Carousel fast path: pre-rendered frames ready — memcpy + border overlay only
  if (carouselFramesReady) {
    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const int bookCount = static_cast<int>(recentBooks.size());
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int centerIdx = inCarouselRow ? selectorIndex : lastCarouselBookIndex;
    int slotIdx = gCarouselCache.findFrameSlot(centerIdx);

    if (frameBuffer && slotIdx < 0 && gCarouselCache.keyHash != 0 && bookCount > 0) {
      const int evictSlot = chooseCarouselEvictionSlot(centerIdx, bookCount);
      if (evictSlot >= 0 && loadCarouselFrameFromDisk(gCarouselCache.keyHash, bookCount, centerIdx, evictSlot)) {
        slotIdx = evictSlot;
      }
    }

    if (frameBuffer && slotIdx >= 0 && carouselFrames[slotIdx]) {
      memcpy(frameBuffer, carouselFrames[slotIdx], renderer.getBufferSize());
      LyraCarouselTheme::setPreRenderIndex(centerIdx);

      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             recentBooks, centerIdx, inCarouselRow);
      if (!inCarouselRow) {
        if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) ==
            CrossPointSettings::UI_THEME::LYRA_CAROUSEL) {
          // Build menu items to pass to the overlay renderer
          std::vector<std::string> menuLabels;
          std::vector<UIIcon> menuIcons;
          menuLabels.reserve(5);
          menuIcons.reserve(5);
          menuLabels.push_back("Open Book");
          menuIcons.push_back(Book);
          menuLabels.push_back("My Library");
          menuIcons.push_back(Folder);
          if (core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl})) {
            menuLabels.push_back("OPDS Browser");
            menuIcons.push_back(Library);
          }
          menuLabels.push_back("File Transfer");
          menuIcons.push_back(Transfer);
          menuLabels.push_back("Settings");
          menuIcons.push_back(Settings);
          const int menuOverlayIdx = selectorIndex - bookCount;
          static_cast<const LyraCarouselTheme&>(GUI).drawButtonMenuSelectionOverlay(
              renderer, static_cast<int>(menuLabels.size()), menuOverlayIdx,
              [&menuLabels](int index) { return menuLabels[index]; },
              [&menuIcons](int index) { return menuIcons[index]; });
        }
      }

      renderer.displayBuffer();
      updateSlidingWindowCache(centerIdx, bookCount);
      if (!firstRenderDone) {
        firstRenderDone = true;
        requestUpdate();
      } else if (!recentsLoaded && !recentsLoading) {
        recentsLoading = true;
        loadRecentCovers(metrics.homeCoverHeight);
      }
      return;
    }
  }

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();
  // If we are using the new media picker UI, use its specialized rendering
  const bool mediaPickerEnabled = core::FeatureModules::hasCapability(core::Capability::HomeMediaPicker);
  if (mediaPickerEnabled) {
    const bool forkDrift = SETTINGS.uiTheme == CrossPointSettings::FORK_DRIFT;
    const int coverSelector = forkDrift && inButtonGrid ? -1 : selectedBookIndex;
    const int menuSelector = forkDrift && !inButtonGrid ? -1 : selectedMenuIndex;

    const int bookCountRender = static_cast<int>(recentBooks.size());
    const int singleRowH = metrics.homeCoverTileHeight / 2;
    const int coverTileH_raw = forkDrift ? ((bookCountRender > 3 ? 2 : 1) * singleRowH) : metrics.homeCoverTileHeight;
    // Clamp cover height so the button menu always has room for at least one row.
    // Without this, landscape orientation (480px) with 2 cover rows (436px) yields
    // a negative menu rect height: 480 − (436 + verticalSpacing×2 + buttonHintsHeight) = −28.
    const int menuMinH = metrics.verticalSpacing * 2 + metrics.buttonHintsHeight + metrics.menuRowHeight;
    const int coverTileH = forkDrift ? std::min(coverTileH_raw, usablePageHeight - menuMinH) : coverTileH_raw;

    GUI.drawRecentBookCover(renderer, Rect(0, topInset, pageWidth, coverTileH), recentBooks, coverSelector,
                            coverRendered, coverBufferStored, bufferRestored, [this]() { return storeCoverBuffer(); },
                            currentBookProgressPercent);

    std::vector<std::string> menuLabels;
    std::vector<UIIcon> menuIcons;
    menuLabels.reserve(6);
    menuIcons.reserve(6);

    if (forkDrift) {
      const bool pokemonPartyHomeMode = isPokemonPartyHomeMode();
      menuLabels.push_back(tr(STR_BOOKS));
      menuIcons.push_back(Folder);
      if (pokemonPartyHomeMode) {
        menuLabels.clear();
        menuIcons.clear();
        menuLabels.push_back(tr(STR_SETTINGS_TITLE));
        menuIcons.push_back(Settings);
      } else {
        if (core::HomeActionRegistry::shouldExpose("todo_planner", {false})) {
          menuLabels.push_back("Agenda");
          menuIcons.push_back(Text);
        }
        if (core::HomeActionRegistry::shouldExpose("anki", {false})) {
          menuLabels.push_back("Anki");
          menuIcons.push_back(Text);  // Using Text icon as placeholder
        }
        menuLabels.push_back(tr(STR_FILE_TRANSFER));
        menuIcons.push_back(Transfer);
        menuLabels.push_back(tr(STR_SETTINGS_TITLE));
        menuIcons.push_back(Settings);
      }
    } else {
      menuLabels.push_back(recentBooks.empty() ? "Open Book (empty)" : "Open Book");
      menuIcons.push_back(Book);
      menuLabels.push_back("My Library");
      menuIcons.push_back(Folder);
      if (core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl})) {
        menuLabels.push_back("OPDS Browser");
        menuIcons.push_back(Library);
      }
      if (core::HomeActionRegistry::shouldExpose("todo_planner", {false})) {
        menuLabels.push_back("TODO");
        menuIcons.push_back(Text);
      }
      if (core::HomeActionRegistry::shouldExpose("anki", {false})) {
        menuLabels.push_back("Anki");
        menuIcons.push_back(Text);
      }
      menuLabels.push_back("File Transfer");
      menuIcons.push_back(Transfer);
      menuLabels.push_back("Settings");
      menuIcons.push_back(Settings);
    }

    GUI.drawButtonMenu(
        renderer,
        Rect{0, topInset + coverTileH + metrics.verticalSpacing, pageWidth,
             usablePageHeight - (coverTileH + metrics.verticalSpacing * 2 + metrics.buttonHintsHeight)},
        static_cast<int>(menuLabels.size()), menuSelector, [&menuLabels](const int index) { return menuLabels[index]; },
        [&menuIcons](const int index) { return menuIcons[index]; });

    const char* backLabel = isPokemonPartyHomeMode() ? "Party" : "";
    const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    constexpr int margin = 20;

    // --- Top "book" card for the current title (selectorIndex == 0) ---
    const int bookWidth = pageWidth / 2;
    const int bookHeight = pageHeight / 2;
    const int bookX = (pageWidth - bookWidth) / 2;
    const int bookY = topInset + 30;
    const bool bookSelected = hasContinueReading && selectorIndex == 0;

    // Bookmark dimensions (used in multiple places)
    const int bookmarkWidth = bookWidth / 8;
    const int bookmarkHeight = bookHeight / 5;
    const int bookmarkX = bookX + bookWidth - bookmarkWidth - 10;
    const int bookmarkY = bookY + 5;

    // Draw book card regardless, fill with message based on `hasContinueReading`
    {
      // Draw cover image as background if available (inside the box)
      // Only load from SD on first render, then use stored buffer
      if (hasContinueReading && hasCoverImage && !coverBmpPath.empty() && !coverRendered) {
        // First time: load cover from SD and render
        SpiBusMutex::Guard guard;
        FsFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // Calculate position to center image within the book card
            int coverX, coverY;

            if (bitmap.getWidth() > bookWidth || bitmap.getHeight() > bookHeight) {
              const float imgRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
              const float boxRatio = static_cast<float>(bookWidth) / static_cast<float>(bookHeight);

              if (imgRatio > boxRatio) {
                coverX = bookX;
                coverY = bookY + (bookHeight - static_cast<int>(bookWidth / imgRatio)) / 2;
              } else {
                coverX = bookX + (bookWidth - static_cast<int>(bookHeight * imgRatio)) / 2;
                coverY = bookY;
              }
            } else {
              coverX = bookX + (bookWidth - bitmap.getWidth()) / 2;
              coverY = bookY + (bookHeight - bitmap.getHeight()) / 2;
            }

            // Draw the cover image centered within the book card
            renderer.drawBitmap(bitmap, coverX, coverY, bookWidth, bookHeight);

            // Draw border around the card
            renderer.drawRect(bookX, bookY, bookWidth, bookHeight);

            // Store the buffer with cover image for fast navigation
            coverBufferStored = storeCoverBuffer();
            coverRendered = true;

            // First render: if selected, draw selection indicators now
            if (bookSelected) {
              renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
              renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
            }
          }
          file.close();
        }
      } else if (!bufferRestored && !coverRendered) {
        // No cover image: draw border or fill, plus bookmark as visual flair
        if (bookSelected) {
          renderer.fillRect(bookX, bookY, bookWidth, bookHeight);
        } else {
          renderer.drawRect(bookX, bookY, bookWidth, bookHeight);
        }

        // Draw bookmark ribbon when no cover image (visual decoration)
        if (hasContinueReading) {
          const int notchDepth = bookmarkHeight / 3;
          const int centerX = bookmarkX + bookmarkWidth / 2;

          const int xPoints[5] = {
              bookmarkX,                  // top-left
              bookmarkX + bookmarkWidth,  // top-right
              bookmarkX + bookmarkWidth,  // bottom-right
              centerX,                    // center notch point
              bookmarkX                   // bottom-left
          };
          const int yPoints[5] = {
              bookmarkY,                                // top-left
              bookmarkY,                                // top-right
              bookmarkY + bookmarkHeight,               // bottom-right
              bookmarkY + bookmarkHeight - notchDepth,  // center notch point
              bookmarkY + bookmarkHeight                // bottom-left
          };

          // Draw bookmark ribbon (inverted if selected)
          renderer.fillPolygon(xPoints, yPoints, 5, !bookSelected);
        }
      }

      // If buffer was restored, draw selection indicators if needed
      if (bufferRestored && bookSelected && coverRendered) {
        // Draw selection border (no bookmark inversion needed since cover has no bookmark)
        renderer.drawRect(bookX + 1, bookY + 1, bookWidth - 2, bookHeight - 2);
        renderer.drawRect(bookX + 2, bookY + 2, bookWidth - 4, bookHeight - 4);
      }
    }

    if (hasContinueReading) {
      // Split into words
      std::vector<std::string> words;
      size_t pos = 0;
      while (pos < lastBookTitle.size()) {
        while (pos < lastBookTitle.size() && lastBookTitle[pos] == ' ') ++pos;
        if (pos >= lastBookTitle.size()) break;
        size_t start = pos;
        while (pos < lastBookTitle.size() && lastBookTitle[pos] != ' ') ++pos;
        words.push_back(lastBookTitle.substr(start, pos - start));
      }

      std::vector<std::string> lines;
      std::string currentLine;
      const int maxLineWidth = bookWidth - 40;
      const int spaceWidth = renderer.getSpaceWidth(UI_12_FONT_ID);

      for (auto& word : words) {
        if (lines.size() >= 3) {
          lines.back() += "...";
          break;
        }
        int wordWidth = renderer.getTextWidth(UI_12_FONT_ID, word.c_str());
        if (wordWidth > maxLineWidth) {
          while (renderer.getTextWidth(UI_12_FONT_ID, (word + "...").c_str()) > maxLineWidth && !word.empty()) {
            utf8RemoveLastChar(word);
          }
          word += "...";
        }

        int curWidth = renderer.getTextWidth(UI_12_FONT_ID, currentLine.c_str());
        if (!currentLine.empty() && curWidth + spaceWidth + wordWidth > maxLineWidth) {
          lines.push_back(currentLine);
          currentLine = word;
        } else {
          if (!currentLine.empty()) currentLine += " ";
          currentLine += word;
        }
      }
      if (!currentLine.empty() && lines.size() < 3) lines.push_back(currentLine);

      int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * lines.size();
      if (!lastBookAuthor.empty()) totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 1.5;

      int titleYStart = bookY + (bookHeight - totalTextHeight) / 2;

      if (coverRendered) {
        // Draw background box for text legibility over cover
        const int maxW =
            std::accumulate(lines.begin(), lines.end(), 0, [this](const int maxWidth, const std::string& line) {
              return std::max(maxWidth, renderer.getTextWidth(UI_12_FONT_ID, line.c_str()));
            });
        int boxW = maxW + 16;
        int boxH = totalTextHeight + 16;
        renderer.fillRect((pageWidth - boxW) / 2, titleYStart - 8, boxW, boxH, bookSelected);
        renderer.drawRect((pageWidth - boxW) / 2, titleYStart - 8, boxW, boxH, !bookSelected);
      }

      for (const auto& l : lines) {
        renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, l.c_str(), !bookSelected);
        titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
      }

      if (!lastBookAuthor.empty()) {
        titleYStart += renderer.getLineHeight(UI_10_FONT_ID) * 0.5;
        std::string author = lastBookAuthor;
        if (renderer.getTextWidth(UI_10_FONT_ID, author.c_str()) > maxLineWidth) {
          while (renderer.getTextWidth(UI_10_FONT_ID, (author + "...").c_str()) > maxLineWidth && !author.empty()) {
            utf8RemoveLastChar(author);
          }
          author += "...";
        }
        renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, author.c_str(), !bookSelected);
      }

      const int continueY = bookY + bookHeight - renderer.getLineHeight(UI_10_FONT_ID) * 1.5;
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, "Continue Reading", !bookSelected);
    } else {
      int y = bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
      renderer.drawCenteredText(UI_12_FONT_ID, y, "No open book");
    }

    // Draw other menu items
    int menuStartY = bookY + bookHeight + 30;
    int menuTileWidth = pageWidth - 40;
    int menuTileHeight = 45;
    int menuSpacing = 10;

    std::vector<const char*> labels_text = {"My Library"};
    if (core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsUrl})) {
      labels_text.push_back("OPDS Browser");
    }
    if (core::HomeActionRegistry::shouldExpose("todo_planner", {false})) {
      labels_text.push_back("TODO");
    }
    if (core::HomeActionRegistry::shouldExpose("anki", {false})) {
      labels_text.push_back("Anki");
    }
    labels_text.push_back("File Transfer");
    labels_text.push_back("Settings");
    for (size_t i = 0; i < labels_text.size(); ++i) {
      int tileY = menuStartY + i * (menuTileHeight + menuSpacing);
      bool selected = (selectorIndex == (int)i + (hasContinueReading ? 1 : 0));
      if (selected)
        renderer.fillRect(20, tileY, menuTileWidth, menuTileHeight);
      else
        renderer.drawRect(20, tileY, menuTileWidth, menuTileHeight);
      renderer.drawCenteredText(UI_10_FONT_ID, tileY + (menuTileHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                                labels_text[i], !selected);
    }

    const auto hints = mappedInput.mapLabels("", "Select", "Up", "Down");
    GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  }

  if (!SETTINGS.isGlobalStatusBarEnabled() && WiFi.status() == WL_CONNECTED) {
    char wifiStr[22];
    const IPAddress ip = WiFi.localIP();
    snprintf(wifiStr, sizeof(wifiStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    const int wifiY = pageHeight - metrics.buttonHintsHeight +
                      (metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
    renderer.drawText(SMALL_FONT_ID, 5, wifiY, wifiStr);
  }

  const bool doFull = !firstRenderDone && APP_STATE.pendingHomeFullRefresh;
  if (doFull) APP_STATE.pendingHomeFullRefresh = false;
  renderer.displayBuffer(doFull ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    loadRecentCovers(metrics.homeCoverHeight);
  }

  if (carouselWarmupPending && !carouselFramesReady) {
    carouselWarmupPending = false;
    const bool showedWarmupProgress = preRenderCarouselFrames(true);
    if (carouselFramesReady || showedWarmupProgress) {
      requestUpdate();
    }
  }
}

void HomeActivity::onContinueReading() { activityManager.goToReader(APP_STATE.openEpubPath); }

void HomeActivity::onMyLibraryOpen() { activityManager.goToMyLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onTodoOpen() { activityManager.goToTodo(); }

void HomeActivity::onAnkiOpen() { activityManager.goToAnki(); }

void HomeActivity::onNotesOpen() { activityManager.goToNotes(); }
