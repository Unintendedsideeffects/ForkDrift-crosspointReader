#include "SleepActivity.h"

#include <ArduinoJson.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#if ENABLE_READING_STATS
#include "../reader/BookStatsView.h"
#endif
#if ENABLE_TODO_PLANNER
#include "activities/todo/TodoItem.h"
#include "activities/todo/TodoPlannerStorage.h"
#endif
#include "BrandScreen.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeatureFlags.h"
#include "RomanClockFontRenderer.h"
#if ENABLE_HAIKU_CLOCK
#include "HaikuClockData.h"
#endif
#include "SleepExtensionHooks.h"
#include "SpiBusMutex.h"
#include "components/UITheme.h"
#include "core/OrientationManager.h"
#include "core/features/FeatureModules.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"
#include "network/background/BackgroundWifiService.h"
#include "util/DateUtils.h"
#include "util/PokemonBookDataStore.h"
#if ENABLE_POKEMON_PARTY
#include "util/BookProgressDataStore.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#endif
#include "util/RecentBooksStore.h"
#include "util/ScreenshotUtil.h"

namespace {

void displaySleepBuffer(const GfxRenderer& renderer) {
  renderer.displayBuffer(SETTINGS.cleanSleepRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);
}

void hideOverlayBatteryStrip(GfxRenderer& renderer) {
  if (!SETTINGS.statusBarBattery) {
    return;
  }

  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) {
    return;
  }

  const int progressBarBand =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS
          ? ((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop
          : 0;
  const int textBandH = metrics.statusBarVerticalMargin;
  const int textBandY = renderer.getScreenHeight() - orientedMarginBottom - progressBarBand - textBandH;
  const int padHPx = features::status_overlay::padH();
  const int textY = textBandY + features::status_overlay::textTop(renderer);
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  static constexpr int bookmarkReserveWidth = 13;
  static constexpr int batteryPercentSpacing = 4;
  const int clearWidth =
      bookmarkReserveWidth + metrics.batteryWidth +
      (showBatteryPercentage ? batteryPercentSpacing + renderer.getTextWidth(SMALL_FONT_ID, "100%") : 0);
  const int clearHeight = std::max(renderer.getTextHeight(SMALL_FONT_ID), metrics.batteryHeight);

  renderer.fillRect(padHPx + orientedMarginLeft, textY, clearWidth, clearHeight, false);
}

// Supported image extensions for sleep images
#if ENABLE_IMAGE_SLEEP
const char* SLEEP_IMAGE_EXTENSIONS[] = {".bmp", ".png", ".jpg", ".jpeg"};
#else
const char* SLEEP_IMAGE_EXTENSIONS[] = {".bmp"};  // BMP only when PNG/JPEG disabled
#endif
constexpr int NUM_SLEEP_IMAGE_EXTENSIONS = sizeof(SLEEP_IMAGE_EXTENSIONS) / sizeof(SLEEP_IMAGE_EXTENSIONS[0]);

bool isSupportedSleepImage(const std::string& filename) {
  if (filename.length() < 4) return false;
  std::string lowerFilename = filename;
  std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (int i = 0; i < NUM_SLEEP_IMAGE_EXTENSIONS; i++) {
    size_t extLen = strlen(SLEEP_IMAGE_EXTENSIONS[i]);
    if (lowerFilename.length() >= extLen &&
        lowerFilename.substr(lowerFilename.length() - extLen) == SLEEP_IMAGE_EXTENSIONS[i]) {
      return true;
    }
  }
  return false;
}

bool isBmpFile(const std::string& filename) {
  if (filename.length() < 4) return false;
  std::string lowerFilename = filename;
  std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowerFilename.substr(lowerFilename.length() - 4) == ".bmp";
}

namespace SleepCacheMutex {
StaticSemaphore_t mutexBuffer;
SemaphoreHandle_t get() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutexStatic(&mutexBuffer);
  return mutex;
}
void lock() {
  auto mutex = get();
  if (mutex) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  }
}
void unlock() {
  auto mutex = get();
  if (mutex == nullptr) {
    return;
  }
  const TaskHandle_t self = xTaskGetCurrentTaskHandle();
  const TaskHandle_t holder = xSemaphoreGetMutexHolder(mutex);
  if (holder != nullptr && holder != self) {
    LOG_ERR("SLP", "skip sleep cache give (not holder): self='%s' holder='%s'", pcTaskGetName(self),
            holder ? pcTaskGetName(holder) : "<none>");
    return;
  }
  xSemaphoreGiveRecursive(mutex);
}
struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
};
}  // namespace SleepCacheMutex

struct SleepImageCache {
  bool scanned = false;
  uint8_t sourceMode = 0xFF;
  uint16_t count = 0;
};

SleepImageCache sleepImageCache;

static constexpr char SLEEP_CACHE_FILE[] = "/.crosspoint/sleep_cache.bin";
static constexpr uint8_t SLEEP_CACHE_VERSION = 1;

static bool loadSleepImageCacheFromFile(SleepImageCache& cache) {
  SpiBusMutex::Guard guard;
  HalFile f;
  if (!Storage.openFileForRead("SLP", SLEEP_CACHE_FILE, f)) return false;

  uint8_t version, sourceMode;
  uint16_t count;
  if (f.read(&version, 1) != 1 || version != SLEEP_CACHE_VERSION || f.read(&sourceMode, 1) != 1 ||
      f.read(&count, 2) != 2) {
    f.close();
    return false;
  }
  f.close();
  cache.sourceMode = sourceMode;
  cache.count = count;
  cache.scanned = true;
  return true;
}

// Context for streaming valid paths directly to the cache file during scan.
// Avoids accumulating all paths in RAM — only one path string exists at a time.
struct CacheWriteCtx {
  HalFile file;
  uint16_t count = 0;
};

static void writeToCacheFile(void* ctx, const std::string& path) {
  auto* w = static_cast<CacheWriteCtx*>(ctx);
  const uint16_t len = static_cast<uint16_t>(path.size());
  w->file.write(&len, 2);
  w->file.write(reinterpret_cast<const void*>(path.data()), len);
  w->count++;
}

static void countValidFile(void* ctx, const std::string& /*path*/) { (*static_cast<uint16_t*>(ctx))++; }

// Reads the path stored at `index` in the binary cache file without loading all paths.
// Seeks sequentially through the variable-length entries; fine since sleep renders are infrequent.
static std::string readSleepImageAtIndex(const uint16_t index) {
  SpiBusMutex::Guard guard;
  HalFile f;
  if (!Storage.openFileForRead("SLP", SLEEP_CACHE_FILE, f)) return "";
  if (!f.seek64(4)) {
    f.close();
    return "";
  }  // skip: version(1)+sourceMode(1)+count(2)
  for (uint16_t i = 0;; i++) {
    uint16_t len;
    if (f.read(&len, 2) != 2 || len > 500) {
      f.close();
      return "";
    }
    if (i == index) {
      std::string path(len, '\0');
      if (static_cast<uint16_t>(f.read(path.data(), len)) != len) {
        f.close();
        return "";
      }
      f.close();
      return path;
    }
    if (!f.seekCur(static_cast<int64_t>(len))) {
      f.close();
      return "";
    }
  }
}

bool tryRenderExternalSleepApp(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  const bool rendered = SleepExtensionHooks::renderExternalSleepScreen(renderer, mappedInput);
  if (rendered) {
    LOG_DBG("SLP", "External sleep app rendered screen");
  }
  return rendered;
}

#if ENABLE_NOTES || ENABLE_TODO_PLANNER
void drawTextListSleepScreen(GfxRenderer& renderer, const char* title, const std::vector<std::string>& rows,
                             const char* emptyText) {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  static constexpr int kPad = 34;
  static constexpr int kTitleGap = 18;
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, kPad, title, true, EpdFontFamily::BOLD);
  renderer.drawLine(kPad, kPad + 25, W - kPad, kPad + 25);

  int y = kPad + 25 + kTitleGap;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 6;
  const int maxTextW = W - kPad * 2;
  const int bottom = H - kPad;
  if (rows.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, H / 2, emptyText);
    displaySleepBuffer(renderer);
    return;
  }

  for (const std::string& row : rows) {
    if (y + lineH > bottom) {
      break;
    }
    const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, row.c_str(), maxTextW);
    renderer.drawText(UI_10_FONT_ID, kPad, y, fitted.c_str(), true);
    y += lineH;
  }
  displaySleepBuffer(renderer);
}
#endif

#if ENABLE_NOTES
std::vector<std::string> loadSleepNotesRows() {
  std::vector<std::string> rows;
  SpiBusMutex::Guard guard;
  HalFile file;
  if (!Storage.openFileForRead("SLP", "/notes.txt", file)) {
    return rows;
  }
  constexpr size_t kMaxBytes = 16u * 1024u;
  const size_t size = file.fileSize();
  if (size > kMaxBytes) {
    LOG_ERR("SLP", "Notes sleep file too large (%zu bytes)", size);
    return rows;
  }
  std::string content(size, '\0');
  if (size > 0 && static_cast<size_t>(file.read(content.data(), size)) != size) {
    LOG_ERR("SLP", "Notes sleep file read short");
    return {};
  }

  std::string line;
  for (const char c : content) {
    if (c == '\n' || c == '\r') {
      if (!line.empty()) {
        rows.push_back(line);
        if (rows.size() >= 12) break;
      }
      line.clear();
      continue;
    }
    line.push_back(c);
  }
  if (!line.empty() && rows.size() < 12) {
    rows.push_back(line);
  }
  return rows;
}
#endif

#if ENABLE_TODO_PLANNER
std::vector<std::string> loadPlannerSleepRows() {
  std::vector<std::string> rows;
  const std::string today = DateUtils::currentDate();
  if (today.empty()) {
    return rows;
  }

  std::string content;
  {
    SpiBusMutex::Guard guard;
    const std::string markdownPath = "/daily/" + today + ".md";
    const std::string textPath = "/daily/" + today + ".txt";
    const bool markdownExists = Storage.exists(markdownPath.c_str());
    const bool textExists = Storage.exists(textPath.c_str());
    const std::string targetPath = TodoPlannerStorage::dailyPath(
        today, core::FeatureModules::hasCapability(core::Capability::MarkdownSupport), markdownExists, textExists);
    if (Storage.exists(targetPath.c_str())) {
      content = Storage.readFile(targetPath.c_str()).c_str();
    }
  }

  std::vector<TodoItem> items;
  TodoPlannerStorage::parseFile(content, items);
  for (const TodoItem& item : items) {
    std::string row;
    if (!item.isHeader) {
      row = item.checked ? "[x] " : "[ ] ";
    }
    row += item.text;
    if (!row.empty()) {
      rows.push_back(row);
      if (rows.size() >= 12) break;
    }
  }
  return rows;
}
#endif

const char* getSleepSourceName(const uint8_t sourceMode) {
  switch (sourceMode) {
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_POKEDEX:
      return "pokedex";
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_ALL:
      return "all";
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_SLEEP:
    default:
      return "sleep";
  }
}

std::string getSleepSourcePath(const uint8_t sourceMode) {
  switch (sourceMode) {
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_POKEDEX:
      return "/sleep/pokedex";
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_ALL:
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_SLEEP:
    default:
      return "/sleep";
  }
}

bool shouldScanRecursively(const uint8_t sourceMode) {
  switch (sourceMode) {
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_POKEDEX:
    case CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_ALL:
      return true;
    default:
      return false;
  }
}

std::string getEntryName(const std::string& path) {
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash == std::string::npos) {
    return path;
  }
  return path.substr(lastSlash + 1);
}

std::string joinPath(const std::string& directoryPath, const std::string& entryName) {
  if (entryName.empty()) return directoryPath;
  if (entryName[0] == '/') return entryName;
  if (directoryPath.empty() || directoryPath == "/") return "/" + entryName;
  if (directoryPath.back() == '/') return directoryPath + entryName;
  return directoryPath + "/" + entryName;
}

// NOLINTNEXTLINE(misc-no-recursion) -- intentional: directory tree traversal
void scanSleepImagesInDirectory(const std::string& directoryPath, const bool recursive,
                                void (*onValid)(void*, const std::string&), void* ctx, int& invalidCount) {
  auto dir = Storage.open(directoryPath.c_str());
  if (!(dir && dir.isDirectory())) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const std::string entryName(name);
    const std::string leafName = getEntryName(entryName);
    if (leafName.empty() || leafName[0] == '.') {
      file.close();
      continue;
    }

    const std::string fullPath = joinPath(directoryPath, entryName);
    if (file.isDirectory()) {
      file.close();
      if (recursive) {
        scanSleepImagesInDirectory(fullPath, true, onValid, ctx, invalidCount);
      }
      continue;
    }

    if (!isSupportedSleepImage(leafName)) {
      file.close();
      continue;
    }

    if (isBmpFile(leafName)) {
      Bitmap bitmap(file, true);
      const auto err = bitmap.parseHeaders();
      if (err == BmpReaderError::Ok) {
        onValid(ctx, fullPath);
      } else {
        invalidCount++;
        LOG_ERR("SLP", "Invalid BMP in %s: %s (%s)", directoryPath.c_str(), leafName.c_str(),
                Bitmap::errorToString(err));
      }
      file.close();
      continue;
    }

    const ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(fullPath);
    if (decoder) {
      ImageDimensions dims = {0, 0};
      if (decoder->getDimensions(fullPath, dims) && dims.width > 0 && dims.height > 0) {
        onValid(ctx, fullPath);
        LOG_DBG("SLP", "Valid %s: %s (%dx%d)", decoder->getFormatName(), fullPath.c_str(), dims.width, dims.height);
      } else {
        invalidCount++;
        LOG_ERR("SLP", "Invalid image: %s (could not read dimensions)", fullPath.c_str());
      }
    } else {
      invalidCount++;
      LOG_ERR("SLP", "Invalid image: %s (unsupported format)", fullPath.c_str());
    }
    file.close();
  }
  dir.close();
}

void scanSleepImagesForSource(const uint8_t sourceMode, void (*onValid)(void*, const std::string&), void* ctx,
                              int& invalidCount) {
  const std::string sourcePath = getSleepSourcePath(sourceMode);
  scanSleepImagesInDirectory(sourcePath, shouldScanRecursively(sourceMode), onValid, ctx, invalidCount);
  if (sourceMode == CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_SLEEP) {
    scanSleepImagesInDirectory("/sleep/pokedex", true, onValid, ctx, invalidCount);
  }
}

void validateSleepImagesOnce() {
  SleepCacheMutex::Guard guard;
  uint8_t sourceMode = SETTINGS.sleepScreenSource;
  if (sourceMode >= CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SCREEN_SOURCE_COUNT) {
    sourceMode = CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_SLEEP;
  }

  if (sleepImageCache.scanned && sleepImageCache.sourceMode == sourceMode) {
    return;
  }

  if (loadSleepImageCacheFromFile(sleepImageCache) && sleepImageCache.sourceMode == sourceMode) {
    LOG_INF("SLP", "Loaded %d sleep images from cache", sleepImageCache.count);
    return;
  }

  sleepImageCache.scanned = false;
  sleepImageCache.sourceMode = sourceMode;
  sleepImageCache.count = 0;

  CacheWriteCtx cacheCtx;
  bool cacheOk;
  {
    SpiBusMutex::Guard spiGuard;
    Storage.mkdir("/.crosspoint");
    cacheOk = Storage.openFileForWrite("SLP", SLEEP_CACHE_FILE, cacheCtx.file);
    if (cacheOk) {
      const uint8_t version = SLEEP_CACHE_VERSION;
      cacheCtx.file.write(&version, 1);
      cacheCtx.file.write(&sourceMode, 1);
      const uint16_t placeholder = 0;
      cacheCtx.file.write(&placeholder, 2);
    }
  }

  int scanInvalidCount = 0;
  if (cacheOk) {
    scanSleepImagesForSource(sourceMode, writeToCacheFile, &cacheCtx, scanInvalidCount);
    SpiBusMutex::Guard spiGuard;
    if (cacheCtx.file.seek64(2)) {
      cacheCtx.file.write(&cacheCtx.count, 2);
    }
    cacheCtx.file.close();
    sleepImageCache.count = cacheCtx.count;
  } else {
    uint16_t validCount = 0;
    scanSleepImagesForSource(sourceMode, countValidFile, &validCount, scanInvalidCount);
    sleepImageCache.count = validCount;
  }

  sleepImageCache.scanned = true;
  LOG_INF("SLP", "Source '%s' found %d valid sleep images", getSleepSourceName(sourceMode), sleepImageCache.count);
}

}  // namespace

void invalidateSleepImageCache() {
  SleepCacheMutex::Guard guard;
  sleepImageCache.scanned = false;
  sleepImageCache.sourceMode = 0xFF;
  sleepImageCache.count = 0;
  {
    SpiBusMutex::Guard spiGuard;
    Storage.remove(SLEEP_CACHE_FILE);
  }
  LOG_INF("SLP", "Sleep image cache invalidated");
}

std::string filenameFromPath(const std::string& path) {
  const size_t lastSlash = path.find_last_of('/');
  return lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
}

std::string recentTitleForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  for (const RecentBook& book : books) {
    if (book.path == path && !book.title.empty()) {
      return book.title;
    }
  }
  return {};
}

SleepImageValidationStats validateSleepImagesWithStats() {
  invalidateSleepImageCache();

  uint8_t sourceMode = SETTINGS.sleepScreenSource;
  if (sourceMode >= CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SCREEN_SOURCE_COUNT) {
    sourceMode = CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_SLEEP;
  }

  CacheWriteCtx cacheCtx;
  {
    SpiBusMutex::Guard spiGuard;
    Storage.mkdir("/.crosspoint");
    if (Storage.openFileForWrite("SLP", SLEEP_CACHE_FILE, cacheCtx.file)) {
      const uint8_t version = SLEEP_CACHE_VERSION;
      cacheCtx.file.write(&version, 1);
      cacheCtx.file.write(&sourceMode, 1);
      const uint16_t placeholder = 0;
      cacheCtx.file.write(&placeholder, 2);
    }
  }

  int invalidCount = 0;
  scanSleepImagesForSource(sourceMode, writeToCacheFile, &cacheCtx, invalidCount);

  {
    SpiBusMutex::Guard spiGuard;
    if (cacheCtx.file.seek64(2)) {
      cacheCtx.file.write(&cacheCtx.count, 2);
    }
    cacheCtx.file.close();
  }

  SleepCacheMutex::Guard guard;
  sleepImageCache.scanned = true;
  sleepImageCache.sourceMode = sourceMode;
  sleepImageCache.count = cacheCtx.count;

  LOG_INF("VALIDATE_SLEEP", "Complete: %d valid, %d invalid", sleepImageCache.count, invalidCount);
  return {static_cast<int>(sleepImageCache.count), invalidCount};
}

int validateAndCountSleepImages() { return validateSleepImagesWithStats().valid; }

uint8_t SleepActivity::effectiveSleepMode() const {
  if (SETTINGS.sleepScreenSplit == CrossPointSettings::SLEEP_SPLIT_SMART) {
    return APP_STATE.lastSleepFromReader ? SETTINGS.sleepScreenReader : SETTINGS.sleepScreenHome;
  }
  return SETTINGS.sleepScreen;
}

void SleepActivity::onEnter() {
  Activity::onEnter();

  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }

  // Optional extension point for third-party sleep apps.
  if (tryRenderExternalSleepApp(renderer, mappedInput)) {
    return;
  }

  const bool preserveCurrentScreen = effectiveSleepMode() == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT;

  if (preserveCurrentScreen) {
    renderTransparentSleepScreen();
    return;
  }

  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  switch (effectiveSleepMode()) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      if (!tryRenderCurrentBookCover()) renderCustomSleepScreen();
      return;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      renderCustomSleepScreen();
      return;
#if ENABLE_READING_STATS
    case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP):
      renderReadingStatsSleepScreen();
      return;
#endif  // ENABLE_READING_STATS
#if ENABLE_NOTES
    case (CrossPointSettings::SLEEP_SCREEN_MODE::NOTES_SLEEP):
      renderNotesSleepScreen();
      return;
#endif
#if ENABLE_TODO_PLANNER
    case (CrossPointSettings::SLEEP_SCREEN_MODE::PLANNER_SLEEP):
      renderPlannerSleepScreen();
      return;
#endif
#if ENABLE_ROMAN_CLOCK_SLEEP
    case (CrossPointSettings::SLEEP_SCREEN_MODE::ROMAN_CLOCK_SLEEP):
      renderRomanClockSleepScreen();
      return;
#endif
#if ENABLE_HAIKU_CLOCK
    case (CrossPointSettings::SLEEP_SCREEN_MODE::HAIKU_CLOCK_SLEEP):
      renderHaikuClockSleepScreen();
      return;
#endif
    default:
      renderDefaultSleepScreen();
      return;
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  SpiBusMutex::Guard guard;
  SleepCacheMutex::Guard cacheGuard;

  // Use pinned cover if set
  if (SETTINGS.sleepPinnedPath[0] != '\0') {
    const std::string pinnedPath(SETTINGS.sleepPinnedPath);
    LOG_INF("SLP", "Using pinned sleep cover: %s", pinnedPath.c_str());
    if (isBmpFile(pinnedPath)) {
      HalFile file;
      if (Storage.openFileForRead("SLP", pinnedPath, file)) {
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          file.close();
          return;
        }
        file.close();
        // File in cache is invalid - delete cache so it's rebuilt next time
        LOG_WRN("SLP", "Cached image invalid, clearing cache");
        Storage.remove(SLEEP_CACHE_FILE);
      }
    } else {
      renderImageSleepScreen(pinnedPath);
      return;
    }
    LOG_WRN("SLP", "Pinned sleep cover failed, falling back to random");
  }

#if ENABLE_POKEMON_PARTY
  if (SETTINGS.sleepScreenSource == CrossPointSettings::SLEEP_SCREEN_SOURCE::SLEEP_SOURCE_POKEDEX &&
      !APP_STATE.openEpubPath.empty()) {
    // Preferred: composite the book cover with its assigned Pokémon on-device.
    if (renderPokemonCoverSleepScreen()) {
      return;
    }
    // Fallback: a pre-baked sleep image authored over the web plugin.
    JsonDocument pokemonDoc;
    if (PokemonBookDataStore::loadPokemonDocument(APP_STATE.openEpubPath, pokemonDoc)) {
      const char* sleepImagePath = pokemonDoc["pokemon"]["sleepImagePath"] | "";
      if (sleepImagePath[0] != '\0') {
        const std::string partySleepImagePath(sleepImagePath);
        LOG_INF("SLP", "Using Pokemon party sleep image: %s", partySleepImagePath.c_str());
        if (isBmpFile(partySleepImagePath)) {
          HalFile file;
          if (Storage.openFileForRead("SLP", partySleepImagePath, file)) {
            Bitmap bitmap(file, true);
            if (bitmap.parseHeaders() == BmpReaderError::Ok) {
              renderBitmapSleepScreen(bitmap);
              file.close();
              return;
            }
            file.close();
          }
        } else if (isSupportedSleepImage(partySleepImagePath)) {
          renderImageSleepScreen(partySleepImagePath);
          return;
        }
        LOG_WRN("SLP", "Pokemon party sleep image failed, falling back to random");
      }
    }
  }
#endif

  validateSleepImagesOnce();
  const uint16_t numFiles = sleepImageCache.count;
  if (numFiles > 0) {
    size_t fileIndex;
    if (SETTINGS.sleepCycleMode == CrossPointSettings::SLEEP_CYCLE_SEQUENTIAL) {
      // Sequential: advance to the next image in order
      fileIndex = (numFiles > 1) ? (APP_STATE.lastSleepImage + 1) % numFiles : 0;
    } else {
      // Random: pick a random image, avoiding the last one shown
      fileIndex = random(numFiles);
      if (numFiles > 1 && fileIndex == APP_STATE.lastSleepImage) {
        fileIndex = (fileIndex + 1) % numFiles;
      }
    }
    const bool selectionChanged = (APP_STATE.lastSleepImage != fileIndex);
    APP_STATE.lastSleepImage = fileIndex;
    if (selectionChanged) {
      APP_STATE.saveToFile();
    }
    const std::string filename = readSleepImageAtIndex(static_cast<uint16_t>(fileIndex));
    if (filename.empty()) {
      LOG_ERR("SLP", "Failed to read image at index %d from cache", (int)fileIndex);
      renderDefaultSleepScreen();
      return;
    }
    LOG_INF("SLP", "Loading: %s", filename.c_str());

    if (isBmpFile(filename)) {
      // Use existing BMP rendering path
      HalFile file;
      if (Storage.openFileForRead("SLP", filename, file)) {
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          file.close();
          return;
        }
        LOG_ERR("SLP", "Invalid BMP: %s", filename.c_str());
        file.close();
      }
    } else {
      // Use new PNG/JPEG rendering path
      renderImageSleepScreen(filename);
      return;
    }
  }

  renderDefaultSleepScreen();
}

bool SleepActivity::tryRenderImagePath(const std::string& path, CoverDrawRect* drawnRect) const {
  SpiBusMutex::Guard guard;
  if (isBmpFile(path)) {
    HalFile file;
    if (Storage.openFileForRead("SLP", path, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap, drawnRect);
        file.close();
        return true;
      }
      file.close();
      LOG_WRN("SLP", "BMP header invalid: %s", path.c_str());
    }
  } else {
#if ENABLE_IMAGE_SLEEP
    const ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(path);
    if (decoder) {
      ImageDimensions dims = {0, 0};
      if (decoder->getDimensions(path, dims) && dims.width > 0 && dims.height > 0) {
        renderImageSleepScreen(path, drawnRect);
        return true;
      }
      LOG_WRN("SLP", "Image dimensions invalid: %s", path.c_str());
    } else {
      LOG_WRN("SLP", "No decoder for: %s", path.c_str());
    }
#else
    LOG_WRN("SLP", "Non-BMP image not supported in this build: %s", path.c_str());
#endif
  }
  return false;
}

bool SleepActivity::tryRenderCurrentBookCover() const {
  if (APP_STATE.openEpubPath.empty() || !Storage.exists(APP_STATE.openEpubPath.c_str())) {
    return false;
  }

  const auto homeCardData =
      core::FeatureModules::resolveHomeCardData(APP_STATE.openEpubPath, renderer.getScreenHeight());
  if (homeCardData.coverPath.empty() || !Storage.exists(homeCardData.coverPath.c_str())) {
    return false;
  }

  LOG_INF("SLP", "Smart: trying current book cover: %s", homeCardData.coverPath.c_str());
  CoverDrawRect drawnRect;
  if (!tryRenderImagePath(homeCardData.coverPath, &drawnRect)) {
    LOG_WRN("SLP", "Smart: book cover failed");
    return false;
  }
#if ENABLE_POKEMON_PARTY
  int cx = 0, cy = 0, cw = renderer.getScreenWidth(), ch = renderer.getScreenHeight();
  if (drawnRect.valid) {
    cx = drawnRect.x;
    cy = drawnRect.y;
    cw = drawnRect.w;
    ch = drawnRect.h;
  }
  if (drawPokemonCoverOverlay(APP_STATE.openEpubPath)) {
    displaySleepBuffer(renderer);
  }
#endif
  return true;
}

void SleepActivity::drawLockIcon(const int cx, const int cy) const {
  // White background badge so icon is visible over any content
  renderer.fillRect(cx - 13, cy - 13, 26, 22, false);

  // Shackle (U-shape above body): two vertical lines + horizontal top
  renderer.drawLine(cx - 5, cy - 1, cx - 5, cy - 10);
  renderer.drawLine(cx + 5, cy - 1, cx + 5, cy - 10);
  renderer.drawLine(cx - 5, cy - 10, cx + 5, cy - 10);

  // Body: filled black rectangle with white interior
  renderer.fillRect(cx - 9, cy, 18, 12, true);
  renderer.fillRect(cx - 8, cy + 1, 16, 10, false);

  // Keyhole slot (small black mark in center of body)
  renderer.fillRect(cx - 1, cy + 3, 3, 5, true);
}

#if ENABLE_READING_STATS
void SleepActivity::renderReadingStatsSleepScreen() const {
  BookReadingStats bookStats;
  GlobalReadingStats globalStats = GlobalReadingStats::load();
  std::string bookTitle = tr(STR_READING_STATS);

  const std::string& path = APP_STATE.openEpubPath;
  if (!path.empty()) {
    const std::string recentTitle = recentTitleForPath(path);
    bookTitle = recentTitle.empty() ? filenameFromPath(path) : recentTitle;

    if (FsHelpers::hasEpubExtension(path)) {
      const std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
      bookStats = BookReadingStats::load(cachePath);
    }
  }

  renderBookStatsView(renderer, nullptr, bookTitle, bookStats, globalStats, false);
  displaySleepBuffer(renderer);
}
#endif  // ENABLE_READING_STATS

#if ENABLE_NOTES
void SleepActivity::renderNotesSleepScreen() const {
  drawTextListSleepScreen(renderer, tr(STR_NOTES), loadSleepNotesRows(), tr(STR_NOTES_EMPTY));
}
#endif

#if ENABLE_TODO_PLANNER
void SleepActivity::renderPlannerSleepScreen() const {
  drawTextListSleepScreen(renderer, tr(STR_TODO_HOME_LABEL), loadPlannerSleepRows(), tr(STR_TODO_FRESH_PAGE));
}
#endif

#if ENABLE_ROMAN_CLOCK_SLEEP
void SleepActivity::renderRomanClockSleepScreen() const {
  const std::string clockLabel = DateUtils::currentClockLabel();
  if (clockLabel.empty()) {
    renderDefaultSleepScreen();
    return;
  }

  const RomanClockFontRenderer::LabelParts label = RomanClockFontRenderer::splitLabel(clockLabel);
  if (label.hour.empty()) {
    renderDefaultSleepScreen();
    return;
  }

  if (RomanClockFontRenderer::getFontData(renderer) == nullptr) {
    renderDefaultSleepScreen();
    return;
  }

  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  renderer.clearScreen();

  // ── Content area ─────────────────────────────────────────────────────────
  static constexpr int kInnerPad = 40;
  const int cx = kInnerPad;
  const int cw = W - cx * 2;
  const int cy = kInnerPad;
  const int ch = H - cy * 2;

  const bool hasMinute = !label.minute.empty();

  if (!hasMinute) {
    // ── Hour alone: split into balanced rows, centered in content area ──────
    const auto hourRows = RomanClockFontRenderer::splitHourIntoRows(label.hour);
    const int hourScale = RomanClockFontRenderer::fitMultiRowScale(renderer, hourRows, cw, ch * 7 / 12);
    if (hourScale <= 0) {
      renderDefaultSleepScreen();
      return;
    }
    if (!RomanClockFontRenderer::drawMultiRowText(renderer, hourRows, cx, cy, cw, ch, hourScale)) {
      renderDefaultSleepScreen();
      return;
    }
  } else {
    // ── Stacked layout: hour dominant above a rule, minute subordinate below ─
    //
    // Vertical allocation within the content area (percentages of ch):
    //
    //  ╔═══════════════════════╗
    //  ║  5% — top breathing   ║
    //  ║ 53% — HOUR zone       ║  ← hour glyph centered here
    //  ║  6% — pre-rule gap    ║
    //  ║  ─── thin rule ───    ║  at 64%
    //  ║  4% — post-rule gap   ║
    //  ║ 24% — MINUTE zone     ║  ← minute glyph centered here
    //  ║  8% — bottom breath   ║
    //  ╚═══════════════════════╝

    const int hourZoneTopY = cy + ch * 5 / 100;
    const int hourZoneH = ch * 53 / 100;
    const int ruleY = cy + ch * 64 / 100;
    const int minuteZoneY = cy + ch * 68 / 100;
    const int minuteZoneH = ch * 24 / 100;

    // Hour: split into balanced rows, fit to zone, then center.
    const auto hourRows = RomanClockFontRenderer::splitHourIntoRows(label.hour);
    const int hourScale = RomanClockFontRenderer::fitMultiRowScale(renderer, hourRows, cw, hourZoneH);
    if (hourScale <= 0) {
      renderDefaultSleepScreen();
      return;
    }
    const int hourH = RomanClockFontRenderer::baseTextHeight(renderer) * hourScale;
    if (!RomanClockFontRenderer::drawMultiRowText(renderer, hourRows, cx, hourZoneTopY, cw, hourZoneH, hourScale)) {
      renderDefaultSleepScreen();
      return;
    }

    // Thin centered rule — 30% of content width, 1px tall.
    const int ruleW = cw * 3 / 10;
    renderer.fillRect(cx + (cw - ruleW) / 2, ruleY, ruleW, 1, true);

    // Minute: hard cap at half the hour height to enforce clear visual hierarchy,
    // ensuring the hour always reads as the primary element.
    const int minuteMaxH = std::min(minuteZoneH, hourH / 2);
    const int minuteScale = RomanClockFontRenderer::fitTextScale(renderer, label.minute, cw, minuteMaxH);
    if (minuteScale <= 0) {
      renderDefaultSleepScreen();
      return;
    }
    const int minuteH = RomanClockFontRenderer::baseTextHeight(renderer) * minuteScale;
    const int minuteW = RomanClockFontRenderer::scaledTextWidth(renderer, label.minute, minuteScale);
    const int minuteX = cx + (cw - minuteW) / 2;
    const int minuteY = minuteZoneY + (minuteZoneH - minuteH) / 2;
    if (!RomanClockFontRenderer::drawScaledText(renderer, label.minute, minuteX, minuteY, minuteScale)) {
      renderDefaultSleepScreen();
      return;
    }
  }

  displaySleepBuffer(renderer);
}
#endif

void SleepActivity::renderTransparentSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const uint8_t* fb = renderer.getFrameBuffer();
  if (fb) {
    SpiBusMutex::Guard guard;
    if (!ScreenshotUtil::saveFramebufferAsBmp("/sleep/transparent.bmp", fb, renderer.getDisplayWidth(),
                                              renderer.getDisplayHeight())) {
      LOG_WRN("SLP", "Failed to save transparent sleep screenshot");
    }
  }

  hideOverlayBatteryStrip(renderer);
  drawLockIcon(pageWidth / 2, pageHeight - 14);
  displaySleepBuffer(renderer);
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  BrandScreen::drawLogo(renderer, pageWidth, pageHeight);
  BrandScreen::drawTitle(renderer, pageHeight);
  BrandScreen::drawSubtitle(renderer, pageHeight, "SLEEPING");

  // Make sleep screen dark unless light is selected in settings
  const bool lightScreen = (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) ||
                           (SETTINGS.sleepScreen == CrossPointSettings::FOLLOW_THEME && !SETTINGS.darkMode);

  if (!lightScreen) {
    renderer.invertScreen();
  }

  displaySleepBuffer(renderer);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, CoverDrawRect* drawnRect) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  displaySleepBuffer(renderer);

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  if (drawnRect) {
    drawnRect->x = x;
    drawnRect->y = y;
    drawnRect->w = pageWidth - 2 * x;
    drawnRect->h = pageHeight - 2 * y;
    drawnRect->valid = true;
  }
}

#if ENABLE_POKEMON_PARTY
namespace {
// Resolve a cover thumbnail that actually exists on the SD card. Covers are
// pre-rendered by the home screen as `thumb_<height>.bmp`, so we can only use a
// height that was already generated. Probe screen-height first (best fit) then a
// few common home-grid heights; return "" when nothing usable is cached.
std::string resolveCachedCoverPath(const std::string& coverBmpPath, int screenHeight) {
  if (coverBmpPath.empty()) {
    return "";
  }
  const int candidates[] = {screenHeight, 540, 400, 390, 370, 226, 200, 120};
  for (const int height : candidates) {
    if (height <= 0) {
      continue;
    }
    const std::string path = UITheme::getCoverThumbPath(coverBmpPath, height);
    if (Storage.exists(path.c_str())) {
      return path;
    }
  }
  return "";
}

// Look up the open book's cover path, preferring the in-memory recent-books list
// (what the home screen actually rendered) before falling back to a fresh
// resolve.
std::string coverPathForOpenBook(const std::string& bookPath) {
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path == bookPath) {
      return book.coverBmpPath;
    }
  }
  return RECENT_BOOKS.getDataFromBook(bookPath).coverBmpPath;
}

}  // namespace

bool SleepActivity::renderPokemonCoverSleepScreen() const {
  if (APP_STATE.openEpubPath.empty()) {
    return false;
  }
  const std::string bookPath = APP_STATE.openEpubPath;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const std::string coverPath = resolveCachedCoverPath(coverPathForOpenBook(bookPath), pageHeight);

  renderer.clearScreen();

  int cx = 0, cy = 0, drawW = pageWidth, drawH = pageHeight;

  // --- Cover, centered and scaled to fit the panel ---
  if (!coverPath.empty()) {
    HalFile file;
    if (Storage.openFileForRead("SLP", coverPath, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const int bw = bitmap.getWidth();
        const int bh = bitmap.getHeight();
        // drawBitmap scales down to fit (maxWidth, maxHeight); center the result.
        const float scale = std::min(1.0f, std::min(static_cast<float>(pageWidth) / static_cast<float>(bw),
                                                    static_cast<float>(pageHeight) / static_cast<float>(bh)));
        drawW = static_cast<int>(static_cast<float>(bw) * scale);
        drawH = static_cast<int>(static_cast<float>(bh) * scale);
        cx = (pageWidth - drawW) / 2;
        cy = (pageHeight - drawH) / 2;
        renderer.drawBitmap(bitmap, cx, cy, pageWidth, pageHeight);
      }
      file.close();
    }
  }

  if (!drawPokemonCoverOverlay(bookPath)) {
    return false;
  }
  displaySleepBuffer(renderer);
  return true;
}

bool SleepActivity::drawPokemonCoverOverlay(const std::string& bookPath) const {
  const PokemonAssignment assignment = PokemonProgress::loadForBook(bookPath);
  if (!assignment.valid) {
    return false;
  }

  BookProgressDataStore::ProgressData pd;
  const float percent = BookProgressDataStore::loadProgress(bookPath, pd) ? pd.percent : 0.0f;
  const int level = PokemonProgress::levelForPercent(percent);
  const int speciesId = PokemonProgress::activeSpeciesId(assignment, level);

  const std::string spritePath = PokemonSpriteCache::spritePath(speciesId);
  if (spritePath.empty() || !Storage.exists(spritePath.c_str())) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("SLP", spritePath, file)) {
    return false;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }

  const int bw = bitmap.getWidth();
  const int bh = bitmap.getHeight();

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  // Position in the bottom right corner of the screen, with a 4px margin from the borders
  const int margin = 4;
  int sx = screenW - bw - margin;
  int sy = screenH - bh - margin;

  sx = std::clamp(sx, 0, std::max(0, screenW - bw));
  sy = std::clamp(sy, 0, std::max(0, screenH - bh));

  renderer.drawBitmap1Bit(bitmap, sx, sy, bw, bh);

  file.close();
  return true;
}
#endif  // ENABLE_POKEMON_PARTY

void SleepActivity::renderImageSleepScreen(const std::string& imagePath, CoverDrawRect* drawnRect) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  if (!decoder) {
    LOG_ERR("SLP", "No decoder for: %s", imagePath.c_str());
    return renderDefaultSleepScreen();
  }

  ImageDimensions dims = {0, 0};
  if (!decoder->getDimensions(imagePath, dims) || dims.width <= 0 || dims.height <= 0) {
    LOG_ERR("SLP", "Could not get dimensions for: %s", imagePath.c_str());
    return renderDefaultSleepScreen();
  }

  LOG_INF("SLP", "Image %dx%d, screen %dx%d", dims.width, dims.height, pageWidth, pageHeight);

  // Calculate scale and position. Crop mode uses cover semantics so Pokedex
  // and other sleep art can fill the full display like BMP sleep images do.
  const float scaleX = static_cast<float>(pageWidth) / static_cast<float>(dims.width);
  const float scaleY = static_cast<float>(pageHeight) / static_cast<float>(dims.height);
  float scale;
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
    scale = std::max(scaleX, scaleY);
  } else {
    scale = std::min(scaleX, scaleY);
    if (scale > 1.0f) scale = 1.0f;
  }

  int displayWidth = static_cast<int>(dims.width * scale);
  int displayHeight = static_cast<int>(dims.height * scale);

  // Center the image
  int x = (pageWidth - displayWidth) / 2;
  int y = (pageHeight - displayHeight) / 2;

  LOG_INF("SLP", "Rendering at %d,%d size %dx%d (scale %.2f)", x, y, displayWidth, displayHeight, scale);

  // Clear screen and prepare for rendering
  renderer.clearScreen();

  // Check if grayscale is enabled (no filter selected)
  const bool useGrayscale = SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  // Configure render settings
  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = pageWidth;
  config.maxHeight = pageHeight;
  config.useGrayscale = useGrayscale;
  config.useDithering = true;
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
    config.maxWidth = displayWidth;
    config.maxHeight = displayHeight;
    config.useExactDimensions = true;
  }

  // Render the image to framebuffer (BW pass)
  renderer.setRenderMode(GfxRenderer::BW);
  if (!decoder->decodeToFramebuffer(imagePath, renderer, config)) {
    LOG_ERR("SLP", "Failed to decode: %s", imagePath.c_str());
    return renderDefaultSleepScreen();
  }

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  displaySleepBuffer(renderer);

  // If grayscale is enabled, do additional passes for 4-level grayscale
  if (useGrayscale) {
    // LSB pass
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    decoder->decodeToFramebuffer(imagePath, renderer, config);
    renderer.copyGrayscaleLsbBuffers();

    // MSB pass
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    decoder->decodeToFramebuffer(imagePath, renderer, config);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  if (drawnRect) {
    if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
      drawnRect->x = 0;
      drawnRect->y = 0;
      drawnRect->w = pageWidth;
      drawnRect->h = pageHeight;
    } else {
      drawnRect->x = x;
      drawnRect->y = y;
      drawnRect->w = displayWidth;
      drawnRect->h = displayHeight;
    }
    drawnRect->valid = true;
  }
}

#if ENABLE_HAIKU_CLOCK
void SleepActivity::renderHaikuClockSleepScreen() const {
  int hour = 0;
  int minute = 0;
  bool timeSet = DateUtils::getHourAndMinute(hour, minute);

  if (SETTINGS.haikuClockLandscape) {
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
  }

  renderer.clearScreen();

  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();

  // Compose the haiku at render time: the time line (line1) is chosen for the
  // user's clock format (clockFormat == 1 -> 12-hour, else 24-hour, matching
  // ClockSyncActivity) and joined to the shared body. Storing the two time
  // lines and the body as separate flash strings avoids duplicating every
  // body once per format.
  std::string textStr;
  std::string digitalTime;
  if (!timeSet) {
    textStr = "Time is a shadow,\nMoving across the deep sky,\nWaiting for the sun.";
  } else {
    digitalTime = DateUtils::currentDigitalClockLabel();
    std::time_t now = std::time(nullptr);
    int day_index = 0;
    if (now > 0) {
      day_index = (now / 86400) % K_HAIKU_DAYS;
    }
    int quarter_hour_idx = (hour * 4) + (minute / 15);
    if (quarter_hour_idx < 0) quarter_hour_idx = 0;
    if (quarter_hour_idx > K_HAIKU_SLOTS_PER_DAY - 1) quarter_hour_idx = K_HAIKU_SLOTS_PER_DAY - 1;
    int haiku_idx = (day_index * K_HAIKU_SLOTS_PER_DAY) + quarter_hour_idx;
    const HaikuEntry& entry = K_HAIKUS[haiku_idx];
    const char* timeLine = (SETTINGS.clockFormat == 1) ? entry.line1_12 : entry.line1_24;
    textStr.reserve(96);
    textStr = timeLine;
    textStr += ",\n";
    textStr += entry.body;
  }
  std::string line1, line2, line3;
  size_t pos1 = textStr.find('\n');
  if (pos1 != std::string::npos) {
    line1 = textStr.substr(0, pos1);
    size_t pos2 = textStr.find('\n', pos1 + 1);
    if (pos2 != std::string::npos) {
      line2 = textStr.substr(pos1 + 1, pos2 - pos1 - 1);
      line3 = textStr.substr(pos2 + 1);
    } else {
      line2 = textStr.substr(pos1 + 1);
    }
  } else {
    line1 = textStr;
  }

  // Symmetric, bezel-aware content box. The text's left edge is the box margin
  // (not an arbitrary W/10 offset), so the block sits tidily within the viewable
  // area instead of hugging the left while a tall portrait screen sits empty.
  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  static constexpr int kInnerPad = 40;  // matches the Roman clock content inset
  const int boxLeft = marginLeft + kInnerPad;
  const int boxTop = marginTop + kInnerPad;
  const int boxW = W - boxLeft - marginRight - kInnerPad;
  const int boxH = H - boxTop - marginBottom - kInnerPad;

  // Pick the largest available font whose widest line still fits the box width,
  // so the haiku fills the available width rather than rendering tiny. Haiku
  // lines are full sentences drawn without wrapping, so an unmeasured large font
  // overflows the right edge in portrait — every off-screen pixel trips the
  // bounds check in GfxRenderer::drawPixel and floods the serial log, which can
  // stretch the sleep render past 15s. Fonts are ordered largest-first; the loop
  // keeps the last (smallest) candidate as a fallback if none fit.
  static constexpr int candidateFonts[] = {NOTOSANS_18_FONT_ID,  LEXENDDECA_18_FONT_ID, NOTOSANS_16_FONT_ID,
                                           NOTOSERIF_18_FONT_ID, NOTOSERIF_14_FONT_ID,  UI_12_FONT_ID};
  int fontId = UI_12_FONT_ID;
  for (const int candidate : candidateFonts) {
    if (!renderer.getFontMap().count(candidate)) continue;
    const int w1 = renderer.getTextWidth(candidate, line1.c_str(), EpdFontFamily::BOLD);
    const int w2 = renderer.getTextWidth(candidate, line2.c_str(), EpdFontFamily::BOLD);
    const int w3 = renderer.getTextWidth(candidate, line3.c_str(), EpdFontFamily::BOLD);
    fontId = candidate;
    if (std::max({w1, w2, w3}) <= boxW) break;
  }

  const int lineHeight = renderer.getLineHeight(fontId);
  const int gap = lineHeight / 2;

  // Left-aligned to the box; the three-line block vertically centered in the box.
  const int totalBlockH = 3 * lineHeight + 2 * gap;
  const int startX = boxLeft;
  const int startY = boxTop + std::max(0, (boxH - totalBlockH) / 2);

  if (!line1.empty()) {
    renderer.drawText(fontId, startX, startY, line1.c_str(), true, EpdFontFamily::BOLD);
  }
  if (!line2.empty()) {
    renderer.drawText(fontId, startX, startY + lineHeight + gap, line2.c_str(), true, EpdFontFamily::BOLD);
  }
  if (!line3.empty()) {
    renderer.drawText(fontId, startX, startY + (lineHeight + gap) * 2, line3.c_str(), true, EpdFontFamily::BOLD);
  }

  if (!digitalTime.empty()) {
    const int timeW = renderer.getTextWidth(SMALL_FONT_ID, digitalTime.c_str(), EpdFontFamily::BOLD);
    const int timeH = renderer.getLineHeight(SMALL_FONT_ID);
    const int timeX = SETTINGS.haikuClockLandscape ? boxLeft : boxLeft + std::max(0, boxW - timeW);
    const int timeY = SETTINGS.haikuClockLandscape ? boxTop + std::max(0, boxH - timeH) : boxTop;
    renderer.drawText(SMALL_FONT_ID, timeX, timeY, digitalTime.c_str(), true, EpdFontFamily::BOLD);
  }

  displaySleepBuffer(renderer);

  if (SETTINGS.haikuClockLandscape) {
    OrientationManager::applyUiOrientation(renderer);
  }
}
#endif
