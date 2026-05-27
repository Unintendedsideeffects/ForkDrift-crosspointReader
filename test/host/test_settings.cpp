#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "include/FeatureFlags.h"
#include "src/CrossPointSettings.h"
#include "src/JsonSettingsIO.h"
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
  s.statusBarChapterPageCount = 0;
  s.statusBarBookProgressPercentage = 0;
  s.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
  s.statusBarProgressBarThickness = CrossPointSettings::PROGRESS_BAR_THICK;
  s.statusBarTitle = CrossPointSettings::BOOK_TITLE;
  s.statusBarBattery = 0;
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
  s.sleepTimeoutMinutes = 23;
  s.refreshFrequency = CrossPointSettings::REFRESH_10;
  s.hyphenationEnabled = 1;
  s.screenMargin = 12;
  s.hideBatteryPercentage = CrossPointSettings::HIDE_READER;
  s.uiTheme = CrossPointSettings::LYRA;
  s.longPressButtonBehavior = CrossPointSettings::OFF;
  s.backgroundServerOnCharge = CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0;
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
  s.statusBarChapterPageCount = 1;
  s.statusBarBookProgressPercentage = 1;
  s.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
  s.statusBarProgressBarThickness = CrossPointSettings::PROGRESS_BAR_NORMAL;
  s.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
  s.statusBarBattery = 1;
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
  s.sleepTimeoutMinutes = 10;
  s.refreshFrequency = CrossPointSettings::REFRESH_15;
  s.hyphenationEnabled = 0;
  s.screenMargin = 5;
  s.hideBatteryPercentage = CrossPointSettings::HIDE_NEVER;
  s.uiTheme = CrossPointSettings::LYRA;
  s.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  s.backgroundServerOnCharge = 0;
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
  CHECK(s.statusBarChapterPageCount == 0);
  CHECK(s.statusBarBookProgressPercentage == 0);
  CHECK(s.statusBarProgressBar == CrossPointSettings::BOOK_PROGRESS);
  CHECK(s.statusBarProgressBarThickness == CrossPointSettings::PROGRESS_BAR_THICK);
  CHECK(s.statusBarTitle == CrossPointSettings::BOOK_TITLE);
  CHECK(s.statusBarBattery == 0);
  CHECK(s.extraParagraphSpacing == 0);
  CHECK(s.textAntiAliasing == 0);
  CHECK(s.shortPwrBtn == CrossPointSettings::SLEEP);
  CHECK(s.orientation == CrossPointSettings::LANDSCAPE_CW);
  CHECK(s.fontFamily == savedFontFamily);
  CHECK(s.fontSize == CrossPointSettings::LARGE);
  CHECK(s.lineSpacing == CrossPointSettings::WIDE);
  CHECK(s.paragraphAlignment == CrossPointSettings::CENTER_ALIGN);
  CHECK(s.sleepTimeoutMinutes == 23);
  CHECK(s.refreshFrequency == CrossPointSettings::REFRESH_10);
  CHECK(s.hyphenationEnabled == 1);
  CHECK(s.screenMargin == 12);
  CHECK(s.hideBatteryPercentage == CrossPointSettings::HIDE_READER);
  CHECK(s.longPressButtonBehavior == CrossPointSettings::OFF);
  CHECK(s.backgroundServerOnCharge == (CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0));
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

TEST_CASE("testSettingsIgnoresRemovedBinarySettingsFile") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  CHECK(Storage.writeFile("/.crosspoint/settings.bin", "old-binary-settings"));
  s.sleepScreen = CrossPointSettings::DARK;
  s.fontFamily = CrossPointSettings::BOOKERLY;

  CHECK_FALSE(s.loadFromFile());

  CHECK(s.sleepScreen == CrossPointSettings::DARK);
  CHECK(s.fontFamily == CrossPointSettings::BOOKERLY);
  CHECK(Storage.exists("/.crosspoint/settings.bin"));
  CHECK_FALSE(Storage.exists("/.crosspoint/settings.bin.bak"));
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
