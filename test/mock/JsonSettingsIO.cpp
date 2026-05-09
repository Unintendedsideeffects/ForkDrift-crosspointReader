// Host-test stub for JsonSettingsIO.
// Uses real ArduinoJson serialization for CrossPointSettings save/load.
// Obfuscation is omitted: passwords are stored as plaintext (the same migration
// fallback path that the real loadSettings already handles for older files).
// All other store functions (state, wifi, koreader, recent) are no-ops.

#include "src/JsonSettingsIO.h"

#include <ArduinoJson.h>
// Keep this undef as a defensive guard for host builds that include pthread/time headers.
#undef TIME_UTC
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "src/CrossPointSettings.h"

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;

  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenSource"] = s.sleepScreenSource;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["statusBar"] = s.statusBar;
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
  doc["sleepTimeout"] = s.sleepTimeout;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["screenMargin"] = s.screenMargin;
  doc["opdsServerUrl"] = s.opdsServerUrl;
  doc["opdsUsername"] = s.opdsUsername;
  doc["opdsPassword"] = s.opdsPassword;  // plaintext — no hardware key on host
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["longPressButtonBehavior"] = s.longPressButtonBehavior;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["backgroundServerOnCharge"] = s.backgroundServerOnCharge;
  doc["todoFallbackCover"] = s.todoFallbackCover;
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

  s.sleepScreen = clamp(doc["sleepScreen"] | (uint8_t)S::DARK, S::SLEEP_SCREEN_MODE_COUNT, S::DARK);
  s.sleepScreenSource = clamp(doc["sleepScreenSource"] | (uint8_t)S::SLEEP_SOURCE_SLEEP, S::SLEEP_SCREEN_SOURCE_COUNT,
                              S::SLEEP_SOURCE_SLEEP);
  s.sleepScreenCoverMode =
      clamp(doc["sleepScreenCoverMode"] | (uint8_t)S::FIT, S::SLEEP_SCREEN_COVER_MODE_COUNT, S::FIT);
  s.sleepScreenCoverFilter =
      clamp(doc["sleepScreenCoverFilter"] | (uint8_t)S::NO_FILTER, S::SLEEP_SCREEN_COVER_FILTER_COUNT, S::NO_FILTER);
  s.statusBar = clamp(doc["statusBar"] | (uint8_t)S::FULL, S::STATUS_BAR_MODE_COUNT, S::FULL);
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
  s.sleepTimeout = clamp(doc["sleepTimeout"] | (uint8_t)S::SLEEP_10_MIN, S::SLEEP_TIMEOUT_COUNT, S::SLEEP_10_MIN);
  s.refreshFrequency =
      clamp(doc["refreshFrequency"] | (uint8_t)S::REFRESH_15, S::REFRESH_FREQUENCY_COUNT, S::REFRESH_15);
  s.screenMargin = doc["screenMargin"] | (uint8_t)5;
  s.hideBatteryPercentage =
      clamp(doc["hideBatteryPercentage"] | (uint8_t)S::HIDE_NEVER, S::HIDE_BATTERY_PERCENTAGE_COUNT, S::HIDE_NEVER);
  if (doc["longPressButtonBehavior"].isNull()) {
    const uint8_t oldSkip = doc["longPressChapterSkip"] | (uint8_t)1;
    s.longPressButtonBehavior = oldSkip ? S::CHAPTER_SKIP : S::OFF;
    if (needsResave) *needsResave = true;
  } else {
    s.longPressButtonBehavior = clamp(doc["longPressButtonBehavior"] | (uint8_t)S::CHAPTER_SKIP,
                                      S::LONG_PRESS_BUTTON_BEHAVIOR_COUNT, S::CHAPTER_SKIP);
  }
  s.longPressChapterSkip = (s.longPressButtonBehavior == S::CHAPTER_SKIP) ? 1 : 0;
  s.hyphenationEnabled = doc["hyphenationEnabled"] | (uint8_t)0;
  s.backgroundServerOnCharge = doc["backgroundServerOnCharge"] | (uint8_t)0;
  s.todoFallbackCover = doc["todoFallbackCover"] | (uint8_t)0;
  s.timeMode = clamp(doc["timeMode"] | (uint8_t)S::TIME_MODE_UTC, static_cast<uint8_t>(S::TIME_MODE_MANUAL + 1),
                     S::TIME_MODE_UTC);
  s.timeZoneOffset = doc["timeZoneOffset"] | (uint8_t)12;
  s.lastTimeSyncEpoch = doc["lastTimeSyncEpoch"] | (uint32_t)0;
  s.releaseChannel =
      clamp(doc["releaseChannel"] | (uint8_t)S::RELEASE_STABLE, S::RELEASE_CHANNEL_COUNT, S::RELEASE_STABLE);
  s.uiTheme = clamp(doc["uiTheme"] | (uint8_t)S::LYRA, static_cast<uint8_t>(S::LYRA_EXTENDED + 1), S::LYRA);
  s.fadingFix = doc["fadingFix"] | (uint8_t)0;
  s.darkMode = doc["darkMode"] | (uint8_t)0;
  s.embeddedStyle = doc["embeddedStyle"] | (uint8_t)1;
  s.usbMscPromptOnConnect = doc["usbMscPromptOnConnect"] | (uint8_t)0;
  s.wifiAutoConnect = doc["wifiAutoConnect"] | (uint8_t)0;

  const char* url = doc["opdsServerUrl"] | "";
  strncpy(s.opdsServerUrl, url, sizeof(s.opdsServerUrl) - 1);
  s.opdsServerUrl[sizeof(s.opdsServerUrl) - 1] = '\0';

  const char* user = doc["opdsUsername"] | "";
  strncpy(s.opdsUsername, user, sizeof(s.opdsUsername) - 1);
  s.opdsUsername[sizeof(s.opdsUsername) - 1] = '\0';

  const char* pass = doc["opdsPassword"] | "";
  strncpy(s.opdsPassword, pass, sizeof(s.opdsPassword) - 1);
  s.opdsPassword[sizeof(s.opdsPassword) - 1] = '\0';

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

// ---- Stubs for store types not compiled into host tests ----

class CrossPointState;
class WifiCredentialStore;
class KOReaderCredentialStore;
class RecentBooksStore;

bool JsonSettingsIO::saveState(const CrossPointState&, const char*) { return true; }
bool JsonSettingsIO::loadState(CrossPointState&, const char*) { return false; }
bool JsonSettingsIO::saveWifi(const WifiCredentialStore&, const char*) { return true; }
bool JsonSettingsIO::loadWifi(WifiCredentialStore&, const char*, bool*) { return false; }
bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore&, const char*) { return true; }
bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore&, const char*, bool*) { return false; }
bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore&, const char*) { return true; }
bool JsonSettingsIO::loadRecentBooks(RecentBooksStore&, const char*) { return false; }
