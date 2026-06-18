// Host-test stub for JsonSettingsIO.
// Uses real ArduinoJson serialization for CrossPointSettings save/load.
// Obfuscation is omitted: passwords are stored as plaintext in this host-only
// stub because hardware-key obfuscation is not available here.
// All other store functions (state, wifi, koreader, recent) are no-ops.

#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
// Keep this undef as a defensive guard for host builds that include pthread/time headers.
#undef TIME_UTC
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "src/activities/reader/ReadingStatsStore.h"
#include "util/RecentBooksStore.h"

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenSource"] = s.sleepScreenSource;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;
  doc["extraParagraphSpacing"] = s.extraParagraphSpacing;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["orientation"] = s.orientation;
  doc["frontButtonLayout"] = s.frontButtonLayout;
  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["fontFamily"] = s.fontFamily;
  doc["fontSize"] = s.fontSize;
  doc["lineSpacing"] = s.lineSpacing;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["sleepTimeoutMinutes"] = s.sleepTimeoutMinutes;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["opdsFilenameFormat"] = s.opdsFilenameFormat;
  doc["screenMargin"] = s.screenMargin;
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["longPressButtonBehavior"] = s.longPressButtonBehavior;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["showButtonHints"] = s.showButtonHints;
  doc["backgroundServerOnCharge"] = s.backgroundServerOnCharge;
  doc["timeMode"] = s.timeMode;
  doc["timeZoneOffset"] = s.timeZoneOffset;
  doc["lastTimeSyncEpoch"] = s.lastTimeSyncEpoch;
  doc["releaseChannel"] = s.releaseChannel;
  doc["uiTheme"] = s.uiTheme;
  doc["fadingFix"] = s.fadingFix;
  doc["darkMode"] = s.darkMode;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["usbMscPromptOnConnect"] = s.usbMscPromptOnConnect;
  doc["wifiAutoConnect"] = s.wifiAutoConnect;
  doc["userFontPath"] = s.userFontPath;
  doc["selectedOtaBundle"] = s.selectedOtaBundle;
  doc["installedOtaBundle"] = s.installedOtaBundle;
  doc["installedOtaFeatureFlags"] = s.installedOtaFeatureFlags;

  std::string jsonStr;
  serializeJson(doc, jsonStr);
  return Storage.writeFile(path, String(jsonStr.c_str()));
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  using S = CrossPointSettings;
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  s.sleepScreen = S::normalizeSleepScreenMode(doc["sleepScreen"] | (uint8_t)S::DARK);
  s.sleepScreenSource = clamp(doc["sleepScreenSource"] | (uint8_t)S::SLEEP_SOURCE_SLEEP, S::SLEEP_SCREEN_SOURCE_COUNT,
                              S::SLEEP_SOURCE_SLEEP);
  s.sleepScreenCoverMode =
      clamp(doc["sleepScreenCoverMode"] | (uint8_t)S::FIT, S::SLEEP_SCREEN_COVER_MODE_COUNT, S::FIT);
  s.sleepScreenCoverFilter =
      clamp(doc["sleepScreenCoverFilter"] | (uint8_t)S::NO_FILTER, S::SLEEP_SCREEN_COVER_FILTER_COUNT, S::NO_FILTER);
  s.statusBarChapterPageCount = doc["statusBarChapterPageCount"] | (uint8_t)1;
  s.statusBarBookProgressPercentage = doc["statusBarBookProgressPercentage"] | (uint8_t)1;
  s.statusBarProgressBar = clamp(doc["statusBarProgressBar"] | (uint8_t)S::HIDE_PROGRESS,
                                 S::STATUS_BAR_PROGRESS_BAR_COUNT, S::HIDE_PROGRESS);
  s.statusBarProgressBarThickness = clamp(doc["statusBarProgressBarThickness"] | (uint8_t)S::PROGRESS_BAR_NORMAL,
                                          S::STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT, S::PROGRESS_BAR_NORMAL);
  s.statusBarTitle =
      clamp(doc["statusBarTitle"] | (uint8_t)S::CHAPTER_TITLE, S::STATUS_BAR_TITLE_COUNT, S::CHAPTER_TITLE);
  s.statusBarBattery = doc["statusBarBattery"] | (uint8_t)1;
  s.extraParagraphSpacing = doc["extraParagraphSpacing"] | (uint8_t)1;
  s.textAntiAliasing = doc["textAntiAliasing"] | (uint8_t)1;
  s.shortPwrBtn = clamp(doc["shortPwrBtn"] | (uint8_t)S::IGNORE, S::SHORT_PWRBTN_COUNT, S::IGNORE);
  s.orientation = clamp(doc["orientation"] | (uint8_t)S::PORTRAIT, S::ORIENTATION_COUNT, S::PORTRAIT);
  s.frontButtonLayout = clamp(doc["frontButtonLayout"] | (uint8_t)S::BACK_CONFIRM_LEFT_RIGHT,
                              S::FRONT_BUTTON_LAYOUT_COUNT, S::BACK_CONFIRM_LEFT_RIGHT);
  s.sideButtonLayout =
      clamp(doc["sideButtonLayout"] | (uint8_t)S::PREV_NEXT, S::SIDE_BUTTON_LAYOUT_COUNT, S::PREV_NEXT);
  const bool hasFrontButtonMapping = !(doc["frontButtonBack"].isNull() || doc["frontButtonConfirm"].isNull() ||
                                       doc["frontButtonLeft"].isNull() || doc["frontButtonRight"].isNull());
  if (hasFrontButtonMapping) {
    s.frontButtonBack =
        clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
    s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM,
                                 S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_CONFIRM);
    s.frontButtonLeft =
        clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
    s.frontButtonRight =
        clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
    CrossPointSettings::validateFrontButtonMapping(s);
  } else {
    s.applyFrontButtonLayoutPreset(static_cast<S::FRONT_BUTTON_LAYOUT>(s.frontButtonLayout));
  }
  s.fontFamily = clamp(doc["fontFamily"] | (uint8_t)S::BOOKERLY, S::FONT_FAMILY_COUNT, S::BOOKERLY);
  s.fontSize = clamp(doc["fontSize"] | (uint8_t)S::MEDIUM, S::FONT_SIZE_COUNT, S::MEDIUM);
  s.lineSpacing = clamp(doc["lineSpacing"] | (uint8_t)S::NORMAL, S::LINE_COMPRESSION_COUNT, S::NORMAL);
  s.paragraphAlignment =
      clamp(doc["paragraphAlignment"] | (uint8_t)S::JUSTIFIED, S::PARAGRAPH_ALIGNMENT_COUNT, S::JUSTIFIED);
  s.sleepTimeoutMinutes =
      std::clamp(doc["sleepTimeoutMinutes"] | (uint8_t)10, S::MIN_SLEEP_TIMEOUT_MINUTES, S::MAX_SLEEP_TIMEOUT_MINUTES);
  s.refreshFrequency =
      clamp(doc["refreshFrequency"] | (uint8_t)S::REFRESH_15, S::REFRESH_FREQUENCY_COUNT, S::REFRESH_15);
  s.opdsFilenameFormat = clamp(doc["opdsFilenameFormat"] | (uint8_t)S::OPDS_FILENAME_AUTHOR_TITLE,
                               S::OPDS_FILENAME_FORMAT_COUNT, S::OPDS_FILENAME_AUTHOR_TITLE);
  s.screenMargin = doc["screenMargin"] | (uint8_t)5;
  s.hideBatteryPercentage =
      clamp(doc["hideBatteryPercentage"] | (uint8_t)S::HIDE_NEVER, S::HIDE_BATTERY_PERCENTAGE_COUNT, S::HIDE_NEVER);
  s.longPressButtonBehavior = clamp(doc["longPressButtonBehavior"] | (uint8_t)S::CHAPTER_SKIP,
                                    S::LONG_PRESS_BUTTON_BEHAVIOR_COUNT, S::CHAPTER_SKIP);
  s.hyphenationEnabled = doc["hyphenationEnabled"] | (uint8_t)0;
  s.showButtonHints = doc["showButtonHints"] | (uint8_t)1;
  s.backgroundServerOnCharge = doc["backgroundServerOnCharge"] | (uint8_t)0;
  s.timeMode = clamp(doc["timeMode"] | (uint8_t)S::TIME_MODE_UTC, static_cast<uint8_t>(S::TIME_MODE_MANUAL + 1),
                     S::TIME_MODE_UTC);
  s.timeZoneOffset = doc["timeZoneOffset"] | (uint8_t)12;
  s.lastTimeSyncEpoch = doc["lastTimeSyncEpoch"] | (uint32_t)0;
  s.releaseChannel =
      clamp(doc["releaseChannel"] | (uint8_t)S::RELEASE_STABLE, S::RELEASE_CHANNEL_COUNT, S::RELEASE_STABLE);
  s.uiTheme = clamp(doc["uiTheme"] | (uint8_t)S::FORK_DRIFT, static_cast<uint8_t>(S::LYRA_CAROUSEL + 1), S::FORK_DRIFT);
  s.fadingFix = doc["fadingFix"] | (uint8_t)0;
  s.darkMode = doc["darkMode"] | (uint8_t)0;
  s.embeddedStyle = doc["embeddedStyle"] | (uint8_t)1;
  s.usbMscPromptOnConnect = doc["usbMscPromptOnConnect"] | (uint8_t)0;
  s.wifiAutoConnect = doc["wifiAutoConnect"] | (uint8_t)0;

  const char* userFontPath = doc["userFontPath"] | "";
  strncpy(s.userFontPath, userFontPath, sizeof(s.userFontPath) - 1);
  s.userFontPath[sizeof(s.userFontPath) - 1] = '\0';

  const char* selectedOtaBundle = doc["selectedOtaBundle"] | "";
  strncpy(s.selectedOtaBundle, selectedOtaBundle, sizeof(s.selectedOtaBundle) - 1);
  s.selectedOtaBundle[sizeof(s.selectedOtaBundle) - 1] = '\0';

  const char* installedOtaBundle = doc["installedOtaBundle"] | "";
  strncpy(s.installedOtaBundle, installedOtaBundle, sizeof(s.installedOtaBundle) - 1);
  s.installedOtaBundle[sizeof(s.installedOtaBundle) - 1] = '\0';

  const char* installedOtaFeatureFlags = doc["installedOtaFeatureFlags"] | "";
  strncpy(s.installedOtaFeatureFlags, installedOtaFeatureFlags, sizeof(s.installedOtaFeatureFlags) - 1);
  s.installedOtaFeatureFlags[sizeof(s.installedOtaFeatureFlags) - 1] = '\0';

  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, HalFile& file, bool* needsResave) {
  std::string json;
  uint8_t buffer[128];
  while (true) {
    const size_t bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead == 0) {
      break;
    }
    json.append(reinterpret_cast<const char*>(buffer), bytesRead);
  }
  return loadSettings(s, json.c_str(), needsResave);
}

// ---- Stubs for store types not compiled into host tests ----

class CrossPointState;
class WifiCredentialStore;
class RecentBooksStore;

bool JsonSettingsIO::saveState(const CrossPointState&, const char*) { return true; }
bool JsonSettingsIO::loadState(CrossPointState&, const char*) { return false; }
bool JsonSettingsIO::loadState(CrossPointState&, HalFile&) { return false; }
bool JsonSettingsIO::saveWifi(const WifiCredentialStore&, const char*) { return true; }
bool JsonSettingsIO::loadWifi(WifiCredentialStore&, const char*, bool*) { return false; }
bool JsonSettingsIO::loadWifi(WifiCredentialStore&, HalFile&, bool*) { return false; }
bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore&, const char*) { return true; }

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }

  store.recentBooks.clear();
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.getCount() >= 10) {
      break;
    }
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(std::move(book));
  }
  return true;
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, HalFile& file) {
  FsFileJsonReader reader(file);
  JsonDocument doc;
  if (deserializeJson(doc, reader)) {
    return false;
  }

  store.recentBooks.clear();
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.getCount() >= 10) {
      break;
    }
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(std::move(book));
  }
  return true;
}
bool JsonSettingsIO::saveOpds(const OpdsServerStore&, const char*) { return true; }
bool JsonSettingsIO::loadOpds(OpdsServerStore&, const char*, bool*) { return false; }
bool JsonSettingsIO::loadOpds(OpdsServerStore&, HalFile&, bool*) { return false; }

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["globalPagesTurned"] = store.globalPagesTurned;
  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.books) {
    JsonObject obj = books.add<JsonObject>();
    obj["cachePath"] = book.cachePath;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["totalReadingMs"] = book.totalReadingMs;
    obj["sessions"] = book.sessions;
    obj["totalPagesTurned"] = book.totalPagesTurned;
    obj["lastSessionMs"] = book.lastSessionMs;
    obj["lastProgressPercent"] = book.lastProgressPercent;
    obj["completed"] = book.completed;
    JsonArray days = obj["readingDays"].to<JsonArray>();
    for (const auto& day : book.readingDays) {
      JsonObject dayObj = days.add<JsonObject>();
      dayObj["dayOrdinal"] = day.dayOrdinal;
      dayObj["readingMs"] = day.readingMs;
    }
  }
  JsonArray days = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.readingDays) {
    JsonObject dayObj = days.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  store.books.clear();
  store.readingDays.clear();
  store.globalPagesTurned = doc["globalPagesTurned"] | 0UL;
  JsonArrayConst books = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : books) {
    ReadingBookStats book;
    book.cachePath = obj["cachePath"] | std::string("");
    if (book.cachePath.empty()) {
      continue;
    }
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | 0ULL;
    book.sessions = obj["sessions"] | 0UL;
    book.totalPagesTurned = obj["totalPagesTurned"] | 0UL;
    book.lastSessionMs = obj["lastSessionMs"] | 0UL;
    book.lastProgressPercent = obj["lastProgressPercent"] | 0;
    book.completed = obj["completed"] | false;
    JsonArrayConst bookDays = obj["readingDays"].as<JsonArrayConst>();
    for (JsonObjectConst dayObj : bookDays) {
      const uint32_t dayOrdinal = dayObj["dayOrdinal"] | 0UL;
      const uint64_t readingMs = dayObj["readingMs"] | 0ULL;
      if (dayOrdinal != 0 && readingMs != 0) {
        book.readingDays.push_back(ReadingDayStats{dayOrdinal, readingMs});
      }
    }
    store.books.push_back(std::move(book));
  }
  JsonArrayConst days = doc["readingDays"].as<JsonArrayConst>();
  for (JsonObjectConst dayObj : days) {
    const uint32_t dayOrdinal = dayObj["dayOrdinal"] | 0UL;
    const uint64_t readingMs = dayObj["readingMs"] | 0ULL;
    if (dayOrdinal != 0 && readingMs != 0) {
      store.readingDays.push_back(ReadingDayStats{dayOrdinal, readingMs});
    }
  }
  return true;
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, HalFile& file) {
  FsFileJsonReader reader(file);
  JsonDocument doc;
  if (deserializeJson(doc, reader)) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadReadingStats(store, json.c_str());
}
