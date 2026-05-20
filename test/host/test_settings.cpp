#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "include/FeatureFlags.h"
#include "lib/Serialization/Serialization.h"
#include "src/JsonSettingsIO.h"
#include "src/CrossPointSettings.h"
#include "test/mock/HalStorage.h"

TEST_CASE("testSettingsRoundTrip") {
  // Reset in-memory filesystem between tests.
  Storage.reset();

  CrossPointSettings& s = CrossPointSettings::getInstance();
  const uint8_t savedFontFamily = ENABLE_NOTOSANS_FONTS ? CrossPointSettings::NOTOSANS : CrossPointSettings::BOOKERLY;

  // Set every persisted field to a distinctive non-default canary value.
  s.sleepScreen = CrossPointSettings::LIGHT;
  s.sleepScreenCoverMode = CrossPointSettings::CROP;
  s.sleepScreenCoverFilter = CrossPointSettings::INVERTED_BLACK_AND_WHITE;
  s.sleepScreenSource = CrossPointSettings::SLEEP_SOURCE_POKEDEX;
  s.statusBar = CrossPointSettings::NO_PROGRESS;
  s.extraParagraphSpacing = 0;
  s.textAntiAliasing = 0;
  s.shortPwrBtn = CrossPointSettings::SLEEP;
  s.orientation = CrossPointSettings::LANDSCAPE_CW;
  // Use LEFT_RIGHT_BACK_CONFIRM as the layout preset and apply the matching remap.
  // The JSON format saves and restores the explicit per-button remap fields
  // directly. The test pre-applies the LEFT_RIGHT_BACK_CONFIRM preset values before
  // saving so the assertions verify the correct round-trip of a custom mapping.
  s.frontButtonLayout = CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM;
  s.frontButtonBack = CrossPointSettings::FRONT_HW_LEFT;
  s.frontButtonConfirm = CrossPointSettings::FRONT_HW_RIGHT;
  s.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
  s.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
  s.sideButtonLayout = CrossPointSettings::NEXT_PREV;
  s.fontFamily = savedFontFamily;
  s.fontSize = CrossPointSettings::LARGE;
  s.lineSpacing = CrossPointSettings::WIDE;
  s.paragraphAlignment = CrossPointSettings::CENTER_ALIGN;
  s.sleepTimeout = CrossPointSettings::SLEEP_30_MIN;
  s.refreshFrequency = CrossPointSettings::REFRESH_10;
  s.hyphenationEnabled = 1;
  s.screenMargin = 12;
  strncpy(s.opdsServerUrl, "http://calibre.local:8080", sizeof(s.opdsServerUrl) - 1);
  strncpy(s.opdsUsername, "testuser", sizeof(s.opdsUsername) - 1);
  strncpy(s.opdsPassword, "s3cr3t!", sizeof(s.opdsPassword) - 1);
  s.hideBatteryPercentage = CrossPointSettings::HIDE_READER;
  s.uiTheme = CrossPointSettings::LYRA;
  s.longPressButtonBehavior = CrossPointSettings::OFF;
  s.longPressChapterSkip = 0;
  s.backgroundServerOnCharge = CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0;
  s.todoFallbackCover = 1;
  s.timeMode = CrossPointSettings::TIME_MODE_LOCAL;
  s.timeZoneOffset = 14;
  s.lastTimeSyncEpoch = 1700000000UL;
  s.releaseChannel = CrossPointSettings::RELEASE_NIGHTLY;
  s.usbMscPromptOnConnect = 1;
  s.wifiAutoConnect = CrossPointSettings::supportsBackgroundServerAlwaysMode() ? 1 : 0;
  strncpy(s.userFontPath, "/fonts/MyFont.ttf", sizeof(s.userFontPath) - 1);
  strncpy(s.selectedOtaBundle, "bundle-abc123", sizeof(s.selectedOtaBundle) - 1);
  strncpy(s.installedOtaBundle, "bundle-xyz789", sizeof(s.installedOtaBundle) - 1);
  strncpy(s.installedOtaFeatureFlags, "epub_support,ota_updates", sizeof(s.installedOtaFeatureFlags) - 1);

  // ── Save ──────────────────────────────────────────────────────────────
  CHECK(s.saveToFile());

  // ── Reset persisted fields to defaults, then reload ──────────────────
  s.sleepScreen = CrossPointSettings::DARK;
  s.sleepScreenCoverMode = CrossPointSettings::FIT;
  s.sleepScreenCoverFilter = CrossPointSettings::NO_FILTER;
  s.sleepScreenSource = CrossPointSettings::SLEEP_SOURCE_SLEEP;
  s.statusBar = CrossPointSettings::FULL;
  s.extraParagraphSpacing = 1;
  s.textAntiAliasing = 1;
  s.shortPwrBtn = CrossPointSettings::IGNORE;
  s.orientation = CrossPointSettings::PORTRAIT;
  s.frontButtonLayout = CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT;
  s.sideButtonLayout = CrossPointSettings::PREV_NEXT;
  s.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  s.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  s.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  s.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  s.fontFamily = CrossPointSettings::BOOKERLY;
  s.fontSize = CrossPointSettings::MEDIUM;
  s.lineSpacing = CrossPointSettings::NORMAL;
  s.paragraphAlignment = CrossPointSettings::JUSTIFIED;
  s.sleepTimeout = CrossPointSettings::SLEEP_10_MIN;
  s.refreshFrequency = CrossPointSettings::REFRESH_15;
  s.hyphenationEnabled = 0;
  s.screenMargin = 5;
  s.opdsServerUrl[0] = '\0';
  s.opdsUsername[0] = '\0';
  s.opdsPassword[0] = '\0';
  s.hideBatteryPercentage = CrossPointSettings::HIDE_NEVER;
  s.uiTheme = CrossPointSettings::LYRA;
  s.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  s.longPressChapterSkip = 1;
  s.backgroundServerOnCharge = 0;
  s.todoFallbackCover = 0;
  s.timeMode = CrossPointSettings::TIME_MODE_UTC;
  s.timeZoneOffset = 12;
  s.lastTimeSyncEpoch = 0;
  s.releaseChannel = CrossPointSettings::RELEASE_STABLE;
  s.usbMscPromptOnConnect = 0;
  s.wifiAutoConnect = 0;
  s.userFontPath[0] = '\0';
  s.selectedOtaBundle[0] = '\0';
  s.installedOtaBundle[0] = '\0';
  s.installedOtaFeatureFlags[0] = '\0';

  CHECK(s.loadFromFile());

  // ── Verify every persisted canary value was round-tripped ─────────────
  CHECK(s.sleepScreen == CrossPointSettings::LIGHT);
  CHECK(s.sleepScreenCoverMode == CrossPointSettings::CROP);
  CHECK(s.sleepScreenCoverFilter == CrossPointSettings::INVERTED_BLACK_AND_WHITE);
  CHECK(s.sleepScreenSource == CrossPointSettings::SLEEP_SOURCE_POKEDEX);
  CHECK(s.statusBar == CrossPointSettings::NO_PROGRESS);
  CHECK(s.extraParagraphSpacing == 0);
  CHECK(s.textAntiAliasing == 0);
  CHECK(s.shortPwrBtn == CrossPointSettings::SLEEP);
  CHECK(s.orientation == CrossPointSettings::LANDSCAPE_CW);
  CHECK(s.fontFamily == savedFontFamily);
  CHECK(s.fontSize == CrossPointSettings::LARGE);
  CHECK(s.lineSpacing == CrossPointSettings::WIDE);
  CHECK(s.paragraphAlignment == CrossPointSettings::CENTER_ALIGN);
  CHECK(s.sleepTimeout == CrossPointSettings::SLEEP_30_MIN);
  CHECK(s.refreshFrequency == CrossPointSettings::REFRESH_10);
  CHECK(s.hyphenationEnabled == 1);
  CHECK(s.screenMargin == 12);
  CHECK(std::string(s.opdsServerUrl) == "http://calibre.local:8080");
  CHECK(std::string(s.opdsUsername) == "testuser");
  CHECK(std::string(s.opdsPassword) == "s3cr3t!");
  CHECK(s.hideBatteryPercentage == CrossPointSettings::HIDE_READER);
  CHECK(s.longPressButtonBehavior == CrossPointSettings::OFF);
  CHECK(s.longPressChapterSkip == 0);
  CHECK(s.backgroundServerOnCharge == (CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0));
  CHECK(s.todoFallbackCover == 1);
  CHECK(s.timeMode == CrossPointSettings::TIME_MODE_LOCAL);
  CHECK(s.timeZoneOffset == 14);
  CHECK(s.lastTimeSyncEpoch == 1700000000UL);
  CHECK(s.releaseChannel == CrossPointSettings::RELEASE_NIGHTLY);
  CHECK(s.usbMscPromptOnConnect == 1);
  CHECK(s.wifiAutoConnect == (CrossPointSettings::supportsBackgroundServerAlwaysMode() ? 1 : 0));
  CHECK(std::string(s.userFontPath) == "/fonts/MyFont.ttf");
  CHECK(std::string(s.selectedOtaBundle) == "bundle-abc123");
  CHECK(std::string(s.installedOtaBundle) == "bundle-xyz789");
  CHECK(std::string(s.installedOtaFeatureFlags) == "epub_support,ota_updates");
  // For LEFT_RIGHT_BACK_CONFIRM the preset produces:
  //   back=FRONT_HW_LEFT(2), confirm=FRONT_HW_RIGHT(3),
  //   left=FRONT_HW_BACK(0),  right=FRONT_HW_CONFIRM(1)
  CHECK(s.frontButtonLayout == CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM);
  CHECK(s.frontButtonBack == CrossPointSettings::FRONT_HW_LEFT);
  CHECK(s.frontButtonConfirm == CrossPointSettings::FRONT_HW_RIGHT);
  CHECK(s.frontButtonLeft == CrossPointSettings::FRONT_HW_BACK);
  CHECK(s.frontButtonRight == CrossPointSettings::FRONT_HW_CONFIRM);
}

TEST_CASE("testBackgroundServerModeClamping") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.backgroundServerOnCharge = 1;
  s.wifiAutoConnect = 1;
  s.validateAndClamp();

  CHECK(s.backgroundServerOnCharge == (CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0));
  CHECK(s.wifiAutoConnect == (CrossPointSettings::supportsBackgroundServerAlwaysMode() ? 1 : 0));

  s.setBackgroundServerMode(CrossPointSettings::BACKGROUND_SERVER_ALWAYS);
  if (CrossPointSettings::supportsBackgroundServerAlwaysMode()) {
    CHECK(s.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_ALWAYS);
  } else if (CrossPointSettings::supportsBackgroundServerOnChargeMode()) {
    CHECK(s.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_NEVER);
  } else {
    CHECK(s.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_NEVER);
  }

  s.setBackgroundServerMode(CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE);
  if (CrossPointSettings::supportsBackgroundServerOnChargeMode()) {
    CHECK(s.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE);
  } else {
    CHECK(s.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_NEVER);
  }

  s.setBackgroundServerMode(CrossPointSettings::BACKGROUND_SERVER_NEVER);
  CHECK(s.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_NEVER);
}

TEST_CASE("testSettingsTruncatedLoad") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  // Write a partial file with only a few fields (simulates v1/v2 firmware file).
  // Serialization order: sleepScreen(1), extraParagraphSpacing(2), shortPwrBtn(3), ...
  {
    FsFile file;
    Storage.openFileForWrite("TEST", "/.crosspoint/settings.bin", file);

    const uint8_t version = 4;
    const uint8_t count = 3;  // only 3 fields present
    serialization::writePod(file, version);
    serialization::writePod(file, count);
    const uint8_t sleepVal = CrossPointSettings::LIGHT;
    const uint8_t spacingVal = 0;
    const uint8_t pwrVal = CrossPointSettings::SLEEP;
    serialization::writePod(file, sleepVal);
    serialization::writePod(file, spacingVal);
    serialization::writePod(file, pwrVal);
    file.close();
  }

  // Set fields to non-default values, then load the partial file.
  // Only the 3 written fields should change; the rest stay at their pre-load values.
  s.sleepScreen = CrossPointSettings::DARK;
  s.extraParagraphSpacing = 1;
  s.shortPwrBtn = CrossPointSettings::IGNORE;
  s.fontFamily = CrossPointSettings::BOOKERLY;

  CHECK(s.loadFromFile());

  // Fields 1-3 are in the file and should be updated.
  CHECK(s.sleepScreen == CrossPointSettings::LIGHT);
  CHECK(s.extraParagraphSpacing == 0);
  CHECK(s.shortPwrBtn == CrossPointSettings::SLEEP);
  // Field 8 (fontFamily) was not in the file, so it is unchanged.
  CHECK(s.fontFamily == CrossPointSettings::BOOKERLY);
}

TEST_CASE("testSettingsJsonPreservesSpecialSleepModes") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  CHECK(JsonSettingsIO::loadSettings(s, "{\"sleepScreen\":12}", nullptr));
  CHECK(s.sleepScreen == CrossPointSettings::READING_STATS_SLEEP);

  CHECK(JsonSettingsIO::loadSettings(s, "{\"sleepScreen\":8}", nullptr));
#if ENABLE_ROMAN_CLOCK_SLEEP
  CHECK(s.sleepScreen == CrossPointSettings::ROMAN_CLOCK_SLEEP);
#else
  CHECK(s.sleepScreen == CrossPointSettings::DARK);
#endif
}
