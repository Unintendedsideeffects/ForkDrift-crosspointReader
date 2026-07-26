#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "I18n.h"
#include "I18nKeys.h"
#include "OpdsServerStore.h"
#include "activities/reader/ReadingStatsStore.h"
#include "util/RecentBooksStore.h"
#include "util/WifiCredentialStore.h"

namespace {

template <typename Input>
bool deserializeJsonLogged(JsonDocument& doc, Input&& input, const char* tag) {
  const DeserializationError error = deserializeJson(doc, input);
  if (error) {
    LOG_ERR(tag, "JSON parse error: %s", error.c_str());
    return false;
  }
  return true;
}

bool deserializeJsonFromFile(JsonDocument& doc, HalFile& file, const char* tag) {
  FsFileJsonReader reader(file);
  return deserializeJsonLogged(doc, reader, tag);
}

bool loadStateFromDoc(CrossPointState& s, const JsonDocument& doc) {
  s.openEpubPath = doc["openEpubPath"] | std::string("");
  s.lastSleepImage = doc["lastSleepImage"] | (uint8_t)UINT8_MAX;
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.wifiAutoConnectSkipCount = doc["wifiAutoConnectSkipCount"] | (uint8_t)0;
  s.wifiAutoConnectBackoffLevel = doc["wifiAutoConnectBackoffLevel"] | (uint8_t)0;
  s.wifiAutoConnectWaitingForNewCredential = doc["wifiAutoConnectWaitingForNewCredential"] | false;
  s.pendingBookmarkSpine = doc["pendingBookmarkSpine"] | static_cast<uint16_t>(PENDING_BOOKMARK_SPINE_NONE);
  s.pendingBookmarkProgress = doc["pendingBookmarkProgress"] | PENDING_BOOKMARK_PROGRESS_NONE;
  return true;
}

bool loadSettingsFromDoc(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave) {
  if (needsResave) *needsResave = false;

  using S = CrossPointSettings;
  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  const uint8_t rawSleep = doc["sleepScreen"] | (uint8_t)S::DARK;
  s.sleepScreenSplit =
      clamp(doc["sleepScreenSplit"] | (rawSleep == S::SMART ? S::SLEEP_SPLIT_SMART : S::SLEEP_SPLIT_UNIFIED),
            S::SLEEP_SCREEN_SPLIT_COUNT, S::SLEEP_SPLIT_UNIFIED);

  if (!doc["sleepScreenReader"].isNull()) {
    s.sleepScreenReader = S::normalizeSleepScreenMode(doc["sleepScreenReader"]);
  } else {
    uint8_t legacyReader = doc["smartSleepReaderMode"] | (uint8_t)0;
    s.sleepScreenReader = (legacyReader == 1) ? S::COVER : S::TRANSPARENT;
  }

  if (!doc["sleepScreenHome"].isNull()) {
    s.sleepScreenHome = S::normalizeSleepScreenMode(doc["sleepScreenHome"]);
  } else {
    uint8_t legacyHome = doc["smartSleepHomeMode"] | (uint8_t)0;
    if (legacyHome == 1)
      s.sleepScreenHome = S::HAIKU_CLOCK_SLEEP;
    else if (legacyHome == 2)
      s.sleepScreenHome = S::ROMAN_CLOCK_SLEEP;
    else if (legacyHome == 3)
      s.sleepScreenHome = S::DARK;
    else
      s.sleepScreenHome = S::CUSTOM;
  }

  s.sleepScreen = S::normalizeSleepScreenMode(rawSleep);
  s.sleepScreenSource = clamp(doc["sleepScreenSource"] | (uint8_t)S::SLEEP_SOURCE_SLEEP, S::SLEEP_SCREEN_SOURCE_COUNT,
                              S::SLEEP_SOURCE_SLEEP);
  const char* sleepPinnedPath = doc["sleepPinnedPath"] | "";
  strncpy(s.sleepPinnedPath, sleepPinnedPath, sizeof(s.sleepPinnedPath) - 1);
  s.sleepPinnedPath[sizeof(s.sleepPinnedPath) - 1] = '\0';

  s.sleepScreenCoverMode =
      clamp(doc["sleepScreenCoverMode"] | (uint8_t)S::FIT, S::SLEEP_SCREEN_COVER_MODE_COUNT, S::FIT);
  s.sleepScreenCoverFilter =
      clamp(doc["sleepScreenCoverFilter"] | (uint8_t)S::NO_FILTER, S::SLEEP_SCREEN_COVER_FILTER_COUNT, S::NO_FILTER);
  s.sleepCycleMode =
      clamp(doc["sleepCycleMode"] | (uint8_t)S::SLEEP_CYCLE_RANDOM, S::SLEEP_CYCLE_MODE_COUNT, S::SLEEP_CYCLE_RANDOM);
  s.cleanSleepRefresh = doc["cleanSleepRefresh"] | (uint8_t)0;

#if ENABLE_HAIKU_CLOCK
  s.haikuClockLandscape = doc["haikuClockLandscape"] | (uint8_t)0;
#endif
  s.statusBarChapterPageCount = doc["statusBarChapterPageCount"] | (uint8_t)1;
  s.statusBarBookProgressPercentage = doc["statusBarBookProgressPercentage"] | (uint8_t)1;
  s.statusBarProgressBar = clamp(doc["statusBarProgressBar"] | (uint8_t)S::HIDE_PROGRESS,
                                 S::STATUS_BAR_PROGRESS_BAR_COUNT, S::HIDE_PROGRESS);
  s.statusBarProgressBarThickness = clamp(doc["statusBarProgressBarThickness"] | (uint8_t)S::PROGRESS_BAR_NORMAL,
                                          S::STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT, S::PROGRESS_BAR_NORMAL);
  s.statusBarTitle =
      clamp(doc["statusBarTitle"] | (uint8_t)S::CHAPTER_TITLE, S::STATUS_BAR_TITLE_COUNT, S::CHAPTER_TITLE);
  s.statusBarBattery = doc["statusBarBattery"] | (uint8_t)1;
  s.statusBarClock = doc["statusBarClock"] | (uint8_t)0;
  s.clockUtcOffsetQ = clamp(doc["clockUtcOffsetQ"] | (uint8_t)48, static_cast<uint8_t>(105), static_cast<uint8_t>(48));
  s.clockFormat = clamp(doc["clockFormat"] | (uint8_t)0, static_cast<uint8_t>(2), static_cast<uint8_t>(0));
  s.clockHasBeenSynced = doc["clockHasBeenSynced"] | (uint8_t)0;
  s.extraParagraphSpacing = doc["extraParagraphSpacing"] | (uint8_t)1;
  s.forceParagraphIndents = doc["forceParagraphIndents"] | (uint8_t)0;
  s.textAntiAliasing = doc["textAntiAliasing"] | (uint8_t)1;
  s.shortPwrBtn = clamp(doc["shortPwrBtn"] | (uint8_t)S::IGNORE, S::SHORT_PWRBTN_COUNT, S::IGNORE);
  s.longPwrBtn = clamp(doc["longPwrBtn"] | (uint8_t)S::SLEEP, S::SHORT_PWRBTN_COUNT, S::SLEEP);
#if ENABLE_DOUBLE_TAP_ACTION
  s.doubleTapPwrBtn =
      clamp(doc["doubleTapPwrBtn"] | (uint8_t)S::FORCE_REFRESH, S::SHORT_PWRBTN_COUNT, S::FORCE_REFRESH);
#endif
  s.orientation = clamp(doc["orientation"] | (uint8_t)S::PORTRAIT, S::ORIENTATION_COUNT, S::PORTRAIT);
  s.uiOrientation = clamp(doc["uiOrientation"] | (uint8_t)S::PORTRAIT, S::ORIENTATION_COUNT, S::PORTRAIT);
  s.frontButtonLayout = clamp(doc["frontButtonLayout"] | (uint8_t)S::BACK_CONFIRM_LEFT_RIGHT,
                              S::FRONT_BUTTON_LAYOUT_COUNT, S::BACK_CONFIRM_LEFT_RIGHT);
  s.sideButtonLayout =
      clamp(doc["sideButtonLayout"] | (uint8_t)S::PREV_NEXT, S::SIDE_BUTTON_LAYOUT_COUNT, S::PREV_NEXT);
  s.sideButtonLongPress = clamp(doc["sideButtonLongPress"] | (uint8_t)S::SIDE_LONG_CHAPTER_SKIP,
                                S::SIDE_LONG_PRESS_COUNT, S::SIDE_LONG_CHAPTER_SKIP);
  s.sideButtonOrientationAware = doc["sideButtonOrientationAware"] | (uint8_t)0;
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
  s.frontButtonOrientationAware = clamp(doc["frontButtonOrientationAware"] | (uint8_t)S::FRONT_ORIENTATION_AWARE_OFF,
                                        S::FRONT_ORIENTATION_AWARE_COUNT, S::FRONT_ORIENTATION_AWARE_OFF);
  s.fontFamily = clamp(doc["fontFamily"] | (uint8_t)S::NOTOSERIF, S::FONT_FAMILY_COUNT, S::NOTOSERIF);
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
  s.longPressMenuAction =
      clamp(doc["longPressMenuAction"] | (uint8_t)S::LONG_MENU_OFF, S::LONG_PRESS_MENU_ACTION_COUNT, S::LONG_MENU_OFF);
  s.hyphenationEnabled = doc["hyphenationEnabled"] | (uint8_t)0;
  s.showButtonHints = doc["showButtonHints"] | (uint8_t)1;
  s.focusReadingEnabled = doc["focusReadingEnabled"] | (uint8_t)0;
  s.guideReadingEnabled = doc["guideReadingEnabled"] | (uint8_t)0;
  s.backgroundServerOnCharge = doc["backgroundServerOnCharge"] | (uint8_t)0;
  s.timeMode = clamp(doc["timeMode"] | (uint8_t)S::TIME_MODE_UTC, static_cast<uint8_t>(S::TIME_MODE_MANUAL + 1),
                     S::TIME_MODE_UTC);
  s.timeZoneOffset = doc["timeZoneOffset"] | (uint8_t)12;
  s.autoSyncDayOnBackgroundPing = doc["autoSyncDayOnBackgroundPing"] | (uint8_t)0;
  s.lastTimeSyncEpoch = doc["lastTimeSyncEpoch"] | (uint32_t)0;
  s.releaseChannel =
      clamp(doc["releaseChannel"] | (uint8_t)S::RELEASE_STABLE, S::RELEASE_CHANNEL_COUNT, S::RELEASE_STABLE);
#if ENABLE_FLOW_THEME
  s.uiTheme = clamp(doc["uiTheme"] | (uint8_t)S::FORK_DRIFT, static_cast<uint8_t>(S::FLOW + 1), S::FORK_DRIFT);
#else
  s.uiTheme = clamp(doc["uiTheme"] | (uint8_t)S::FORK_DRIFT, static_cast<uint8_t>(S::TERMINAL + 1), S::FORK_DRIFT);
#endif
  s.recentBooksView =
      clamp(doc["recentBooksView"] | (uint8_t)S::RECENT_BOOKS_LIST, S::RECENT_BOOKS_VIEW_COUNT, S::RECENT_BOOKS_LIST);
  s.fadingFix = doc["fadingFix"] | (uint8_t)0;
  if (doc["darkModeScope"].is<uint8_t>() || doc["darkModeScope"].is<int>()) {
    s.darkModeScope = clamp(doc["darkModeScope"] | (uint8_t)S::DARK_OFF, S::DARK_MODE_SCOPE_COUNT, S::DARK_OFF);
  } else if (doc["darkMode"] | (uint8_t)0) {
    s.darkModeScope = S::DARK_EVERYWHERE;
  } else {
    s.darkModeScope = S::DARK_OFF;
  }
  s.syncDarkModeLegacyField();
  s.embeddedStyle = doc["embeddedStyle"] | (uint8_t)1;
  s.wifiAutoConnect = doc["wifiAutoConnect"] | (uint8_t)0;
  s.showHiddenFiles = doc["showHiddenFiles"] | (uint8_t)0;
  s.todoOpenDirectToToday = doc["todoOpenDirectToToday"] | (uint8_t)0;
  s.moveFinishedToReadFolder = doc["moveFinishedToReadFolder"] | (uint8_t)0;
  s.developerMode = doc["developerMode"] | (uint8_t)0;
  s.imageRendering =
      clamp(doc["imageRendering"] | (uint8_t)S::IMAGES_DISPLAY, S::IMAGE_RENDERING_COUNT, S::IMAGES_DISPLAY);
  // highlight_export::FORMAT_COUNT == 3 (0=Markdown default); literal avoids the
  // feature-gated HighlightExporter include here.
  s.highlightExportFormat = clamp(doc["highlightExportFormat"] | (uint8_t)0, (uint8_t)3, (uint8_t)0);
  s.globalStatusBar = clamp(doc["globalStatusBar"] | (uint8_t)S::GLOBAL_STATUS_BAR_OFF, S::GLOBAL_STATUS_BAR_MODE_COUNT,
                            S::GLOBAL_STATUS_BAR_OFF);
  // settings.json gained a version key in schema 1. Absent → a pre-versioning
  // file, where globalStatusBarPosition 0/1/2 meant TOP/BOTTOM/OFF. The bar is
  // top-only now, so a stored BOTTOM (1) must become ON — leaving it would
  // silently reinterpret it as READER_ONLY and hide the bar outside the reader.
  const uint8_t schemaVersion = doc["settingsVersion"] | (uint8_t)0;
  uint8_t rawStatusBarPosition = doc["globalStatusBarPosition"] | (uint8_t)S::STATUS_BAR_ON;
  if (schemaVersion < 1 && rawStatusBarPosition == 1) {
    rawStatusBarPosition = S::STATUS_BAR_ON;
    if (needsResave) *needsResave = true;
  }
  s.globalStatusBarPosition = clamp(rawStatusBarPosition, S::GLOBAL_STATUS_BAR_POSITION_COUNT, S::STATUS_BAR_ON);

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

  const char* deviceName = doc["deviceName"] | "";
  strncpy(s.deviceName, deviceName, sizeof(s.deviceName) - 1);
  s.deviceName[sizeof(s.deviceName) - 1] = '\0';

  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(s.sdFontFamilyName, sfn, sizeof(s.sdFontFamilyName) - 1);
  s.sdFontFamilyName[sizeof(s.sdFontFamilyName) - 1] = '\0';

  if (doc["language"].is<const char*>()) {
    s.language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  LOG_DBG("CPS", "Settings loaded from file");
  return true;
}

}  // namespace

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  doc["lastSleepImage"] = s.lastSleepImage;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  doc["wifiAutoConnectSkipCount"] = s.wifiAutoConnectSkipCount;
  doc["wifiAutoConnectBackoffLevel"] = s.wifiAutoConnectBackoffLevel;
  doc["wifiAutoConnectWaitingForNewCredential"] = s.wifiAutoConnectWaitingForNewCredential;
  if (s.pendingBookmarkSpine != PENDING_BOOKMARK_SPINE_NONE) {
    doc["pendingBookmarkSpine"] = s.pendingBookmarkSpine;
    doc["pendingBookmarkProgress"] = s.pendingBookmarkProgress;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadState(CrossPointState& s, const char* json) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "CPS")) {
    return false;
  }
  return loadStateFromDoc(s, doc);
}

bool JsonSettingsIO::loadState(CrossPointState& s, HalFile& file) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "CPS")) {
    return false;
  }
  return loadStateFromDoc(s, doc);
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenSource"] = s.sleepScreenSource;
  doc["sleepPinnedPath"] = s.sleepPinnedPath;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["sleepCycleMode"] = s.sleepCycleMode;
  doc["cleanSleepRefresh"] = s.cleanSleepRefresh;
  doc["sleepScreenSplit"] = s.sleepScreenSplit;
  doc["sleepScreenReader"] = s.sleepScreenReader;
  doc["sleepScreenHome"] = s.sleepScreenHome;
#if ENABLE_HAIKU_CLOCK
  doc["haikuClockLandscape"] = s.haikuClockLandscape;
#endif
  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;
  doc["statusBarClock"] = s.statusBarClock;
  doc["clockUtcOffsetQ"] = s.clockUtcOffsetQ;
  doc["clockFormat"] = s.clockFormat;
  doc["clockHasBeenSynced"] = s.clockHasBeenSynced;
  doc["extraParagraphSpacing"] = s.extraParagraphSpacing;
  doc["forceParagraphIndents"] = s.forceParagraphIndents;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["longPwrBtn"] = s.longPwrBtn;
#if ENABLE_DOUBLE_TAP_ACTION
  doc["doubleTapPwrBtn"] = s.doubleTapPwrBtn;
#endif
  doc["orientation"] = s.orientation;
  doc["uiOrientation"] = s.uiOrientation;
  doc["frontButtonLayout"] = s.frontButtonLayout;
  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["sideButtonLongPress"] = s.sideButtonLongPress;
  doc["sideButtonOrientationAware"] = s.sideButtonOrientationAware;
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["frontButtonOrientationAware"] = s.frontButtonOrientationAware;
  doc["fontFamily"] = s.fontFamily;
  if (s.sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = s.sdFontFamilyName;
  }
  doc["fontSize"] = s.fontSize;
  doc["lineSpacing"] = s.lineSpacing;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["sleepTimeoutMinutes"] = s.sleepTimeoutMinutes;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["opdsFilenameFormat"] = s.opdsFilenameFormat;
  doc["screenMargin"] = s.screenMargin;
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["longPressButtonBehavior"] = s.longPressButtonBehavior;
  doc["longPressMenuAction"] = s.longPressMenuAction;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["showButtonHints"] = s.showButtonHints;
  doc["backgroundServerOnCharge"] = s.backgroundServerOnCharge;
  doc["timeMode"] = s.timeMode;
  doc["timeZoneOffset"] = s.timeZoneOffset;
  doc["autoSyncDayOnBackgroundPing"] = s.autoSyncDayOnBackgroundPing;
  doc["lastTimeSyncEpoch"] = s.lastTimeSyncEpoch;
  doc["releaseChannel"] = s.releaseChannel;
  doc["uiTheme"] = s.uiTheme;
  doc["recentBooksView"] = s.recentBooksView;
  doc["fadingFix"] = s.fadingFix;
  doc["darkModeScope"] = s.darkModeScope;
  doc["darkMode"] = s.darkMode;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["focusReadingEnabled"] = s.focusReadingEnabled;
  doc["guideReadingEnabled"] = s.guideReadingEnabled;
  doc["userFontPath"] = s.userFontPath;
  doc["selectedOtaBundle"] = s.selectedOtaBundle;
  doc["installedOtaBundle"] = s.installedOtaBundle;
  doc["installedOtaFeatureFlags"] = s.installedOtaFeatureFlags;
  doc["deviceName"] = s.deviceName;
  doc["wifiAutoConnect"] = s.wifiAutoConnect;
  doc["showHiddenFiles"] = s.showHiddenFiles;
  doc["todoOpenDirectToToday"] = s.todoOpenDirectToToday;
  doc["moveFinishedToReadFolder"] = s.moveFinishedToReadFolder;
  doc["developerMode"] = s.developerMode;
  doc["imageRendering"] = s.imageRendering;
  doc["highlightExportFormat"] = s.highlightExportFormat;
  doc["globalStatusBar"] = s.globalStatusBar;
  doc["globalStatusBarPosition"] = s.globalStatusBarPosition;
  doc["settingsVersion"] = CrossPointSettings::SETTINGS_SCHEMA_VERSION;

  doc["language"] = (s.language < getLanguageCount()) ? LANGUAGE_CODES[s.language] : "EN";

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "CPS")) {
    return false;
  }
  return loadSettingsFromDoc(s, doc, needsResave);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, HalFile& file, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "CPS")) {
    return false;
  }
  return loadSettingsFromDoc(s, doc, needsResave);
}

// ---- WifiCredentialStore ----

bool JsonSettingsIO::saveWifi(const WifiCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["lastConnectedSsid"] = store.getLastConnectedSsid();

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto& cred : store.getCredentials()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "WCS")) {
    return false;
  }
  if (needsResave) *needsResave = false;

  store.lastConnectedSsid = doc["lastConnectedSsid"] | std::string("");

  store.credentials.clear();
  const JsonArrayConst arr = doc["credentials"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.credentials.size() >= store.MAX_NETWORKS) break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");
    bool ok = false;
    cred.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || cred.password.empty()) {
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty() && needsResave) *needsResave = true;
    }
    store.credentials.push_back(cred);
  }

  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", store.credentials.size());
  return true;
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, HalFile& file, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "WCS")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadWifi(store, json.c_str(), needsResave);
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "RBS")) {
    return false;
  }

  store.recentBooks.clear();
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, HalFile& file) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "RBS")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadRecentBooks(store, json.c_str());
}

// ---- OpdsServerStore ----

bool JsonSettingsIO::saveOpds(const OpdsServerStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& srv : store.servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = srv.name;
    obj["url"] = srv.url;
    obj["username"] = srv.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(srv.password);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, const char* json, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "OPS")) {
    return false;
  }
  if (needsResave) {
    *needsResave = false;
  }

  store.servers.clear();
  const JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.servers.size() >= OpdsServerStore::MAX_SERVERS) {
      break;
    }
    OpdsServer srv;
    srv.name = obj["name"] | std::string("");
    srv.url = obj["url"] | std::string("");
    srv.username = obj["username"] | std::string("");
    bool ok = false;
    srv.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || srv.password.empty()) {
      srv.password = obj["password"] | std::string("");
      if (!srv.password.empty() && needsResave) {
        *needsResave = true;
      }
    }
    store.servers.push_back(std::move(srv));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", store.servers.size());
  return true;
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, HalFile& file, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "OPS")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadOpds(store, json.c_str(), needsResave);
}

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

  JsonArray aggregateDays = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.readingDays) {
    JsonObject dayObj = aggregateDays.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "RST")) {
    return false;
  }

  store.books.clear();
  store.readingDays.clear();
  store.lastSessionSnapshot = {};
  store.activeSession = false;
  store.activeBookIndex = 0;
  store.activeAccumulatedMs = 0;
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
    JsonArrayConst days = obj["readingDays"].as<JsonArrayConst>();
    for (JsonObjectConst dayObj : days) {
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
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "RST")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadReadingStats(store, json.c_str());
}
