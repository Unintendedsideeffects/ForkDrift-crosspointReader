#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "include/FeatureFlags.h"
#include "src/CrossPointSettings.h"
#include "src/JsonSettingsIO.h"
#include "src/SettingsList.h"
#include "test/mock/HalStorage.h"

namespace {

const SettingInfo* findSettingByKey(const std::vector<SettingInfo>& settings, const char* key) {
  for (const auto& setting : settings) {
    if (setting.key != nullptr && std::string(setting.key) == key) {
      return &setting;
    }
  }
  return nullptr;
}

size_t optionIndexForValue(const SettingInfo& setting, const uint8_t value) {
  auto it = std::find(setting.enumPersistedValues.begin(), setting.enumPersistedValues.end(), value);
  return it == setting.enumPersistedValues.end()
             ? setting.enumPersistedValues.size()
             : static_cast<size_t>(std::distance(setting.enumPersistedValues.begin(), it));
}

}  // namespace

// Regression: getSettingsList() used a hardcoded reserve(48). Once the real
// count passed it, the 2x regrowth needed the old and new arrays live at once
// (9.6 KB + 19.2 KB for 200-byte entries) and bad_alloc'd on the device's
// fragmented heap -> abort() on every entry to Settings. The reserve must
// always match the count exactly, so no reallocation can happen.
TEST_CASE("getSettingsList reserves exactly, never reallocates") {
  Storage.reset();
  const auto settings = getSettingsList();

  REQUIRE(!settings.empty());
  CHECK(settings.capacity() == settings.size());
}

TEST_CASE("settings metadata keeps Looks and sleep controls available") {
  Storage.reset();
  const auto settings = getSettingsList();

  const SettingInfo* sleepFilter = findSettingByKey(settings, "sleepScreenCoverFilter");
  REQUIRE(sleepFilter != nullptr);
  CHECK(sleepFilter->category == StrId::STR_CAT_DISPLAY);
  CHECK(sleepFilter->visiblePredicate == nullptr);

  const SettingInfo* stayAwakeWhileCharging = findSettingByKey(settings, "stayAwakeWhileCharging");
  REQUIRE(stayAwakeWhileCharging != nullptr);
  CHECK(stayAwakeWhileCharging->type == SettingType::TOGGLE);
  CHECK(stayAwakeWhileCharging->category == StrId::STR_CAT_SYSTEM);

  const SettingInfo* globalStatusBar = findSettingByKey(settings, "globalStatusBarPosition");
#if ENABLE_GLOBAL_STATUS_BAR
  REQUIRE(globalStatusBar != nullptr);
  CHECK(globalStatusBar->category == StrId::STR_CAT_DISPLAY);
#else
  CHECK(globalStatusBar == nullptr);
#endif
}

#if ENABLE_TERMINUS_SLEEP
TEST_CASE("Terminus is a sleep-screen option only after on-device setup") {
  Storage.reset();
  TERMINUS_STORE.clear();

  std::vector<StrId> labels;
  std::vector<uint8_t> values;
  std::vector<const char*> featureKeys;
  buildSleepModeOptions(labels, values, featureKeys, false, false);
  CHECK(optionIndexForValue(SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, nullptr, labels).withEnumPersistedValues(values),
                            CrossPointSettings::TERMINUS_SLEEP) == values.size());

  TERMINUS_STORE.setApiKey("device-token");
  TERMINUS_STORE.setDeviceId("8C:BF:EA:38:92:28");
  labels.clear();
  values.clear();
  featureKeys.clear();
  buildSleepModeOptions(labels, values, featureKeys, false, false);
  const auto terminus = std::find(values.begin(), values.end(), CrossPointSettings::TERMINUS_SLEEP);
  REQUIRE(terminus != values.end());
  CHECK(labels[static_cast<size_t>(std::distance(values.begin(), terminus))] == StrId::STR_TERMINUS);

  TERMINUS_STORE.clear();
}
#endif

TEST_CASE("testSettingsRoundTrip") {
  // Reset in-memory filesystem between tests.
  Storage.reset();

  CrossPointSettings& s = CrossPointSettings::getInstance();
  const uint8_t savedFontFamily = ENABLE_NOTOSANS_FONTS ? CrossPointSettings::NOTOSANS : CrossPointSettings::BOOKERLY;

  // Set every persisted field to a distinctive non-default canary value.
  s.sleepScreen = CrossPointSettings::LIGHT;
  s.sleepScreenCoverMode = CrossPointSettings::CROP;
  s.sleepScreenCoverFilter = CrossPointSettings::INVERTED_BLACK_AND_WHITE;
  s.cleanSleepRefresh = 1;
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
  s.stayAwakeWhileCharging = 1;
  s.refreshFrequency = CrossPointSettings::REFRESH_10;
  s.opdsFilenameFormat = CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR;
  s.hyphenationEnabled = 1;
  s.showButtonHints = 0;
  s.screenMargin = 12;
  s.hideBatteryPercentage = CrossPointSettings::HIDE_READER;
  s.uiTheme = CrossPointSettings::LYRA;
  s.longPressButtonBehavior = CrossPointSettings::OFF;
  s.backgroundServerOnCharge = CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0;
  s.timeMode = CrossPointSettings::TIME_MODE_LOCAL;
  s.timeZoneOffset = 14;
  s.autoSyncDayOnBackgroundPing = 1;
  s.lastTimeSyncEpoch = 1700000000UL;
  s.terminusSleepEnabled = 1;
  s.timedSleepRefreshInterval = 4;
  s.releaseChannel = CrossPointSettings::RELEASE_NIGHTLY;
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
  s.cleanSleepRefresh = 0;
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
  s.stayAwakeWhileCharging = 0;
  s.refreshFrequency = CrossPointSettings::REFRESH_15;
  s.opdsFilenameFormat = CrossPointSettings::OPDS_FILENAME_AUTHOR_TITLE;
  s.hyphenationEnabled = 0;
  s.showButtonHints = 1;
  s.screenMargin = 5;
  s.hideBatteryPercentage = CrossPointSettings::HIDE_NEVER;
  s.uiTheme = CrossPointSettings::LYRA;
  s.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  s.backgroundServerOnCharge = 0;
  s.timeMode = CrossPointSettings::TIME_MODE_UTC;
  s.timeZoneOffset = 12;
  s.autoSyncDayOnBackgroundPing = 0;
  s.lastTimeSyncEpoch = 0;
  s.terminusSleepEnabled = 0;
  s.timedSleepRefreshInterval = 0;
  s.releaseChannel = CrossPointSettings::RELEASE_STABLE;
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
  CHECK(s.cleanSleepRefresh == 1);
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
  CHECK(s.stayAwakeWhileCharging == 1);
  CHECK(s.refreshFrequency == CrossPointSettings::REFRESH_10);
  CHECK(s.opdsFilenameFormat == CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR);
  CHECK(s.hyphenationEnabled == 1);
  CHECK(s.showButtonHints == 0);
  CHECK(s.screenMargin == 12);
  CHECK(s.hideBatteryPercentage == CrossPointSettings::HIDE_READER);
  CHECK(s.longPressButtonBehavior == CrossPointSettings::OFF);
  CHECK(s.backgroundServerOnCharge == (CrossPointSettings::supportsBackgroundServerOnChargeMode() ? 1 : 0));
  CHECK(s.timeMode == CrossPointSettings::TIME_MODE_LOCAL);
  CHECK(s.timeZoneOffset == 14);
  CHECK(s.autoSyncDayOnBackgroundPing == 1);
  CHECK(s.lastTimeSyncEpoch == 1700000000UL);
  CHECK(s.terminusSleepEnabled == 1);
  CHECK(s.timedSleepRefreshInterval == 4);
  CHECK(s.getTimedRefreshIntervalMicros() == 8ULL * 3600ULL * 1000000ULL);
  CHECK(s.releaseChannel == CrossPointSettings::RELEASE_NIGHTLY);
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

TEST_CASE("timed sleep refresh maps every persisted interval and rejects invalid values") {
  CrossPointSettings& s = CrossPointSettings::getInstance();
  static constexpr uint64_t kHourMicros = 3600ULL * 1000000ULL;
  static constexpr uint8_t kHours[] = {0, 1, 2, 4, 8, 24};

  for (uint8_t value = 0; value < sizeof(kHours) / sizeof(kHours[0]); ++value) {
    s.timedSleepRefreshInterval = value;
    CHECK(s.getTimedRefreshIntervalMicros() == static_cast<uint64_t>(kHours[value]) * kHourMicros);
  }

  s.timedSleepRefreshInterval = CrossPointSettings::TIMED_REFRESH_SCREENSAVER;
  CHECK(s.getTimedRefreshIntervalMicros() == 15ULL * 60ULL * 1000000ULL);
  CHECK(s.getTimedRefreshIntervalMicros(60) == 60ULL * 1000000ULL);

  s.terminusSleepEnabled = 7;
  s.timedSleepRefreshInterval = 255;
  s.validateAndClamp();
  CHECK(s.terminusSleepEnabled == 1);
  CHECK(s.timedSleepRefreshInterval == 0);
  CHECK(s.getTimedRefreshIntervalMicros() == 0);
}

TEST_CASE("stay awake while charging only blocks inactivity sleep on USB power") {
  CrossPointSettings& s = CrossPointSettings::getInstance();
  const uint8_t saved = s.stayAwakeWhileCharging;

  s.stayAwakeWhileCharging = 0;
  CHECK_FALSE(s.preventsAutoSleepWhileCharging(false));
  CHECK_FALSE(s.preventsAutoSleepWhileCharging(true));

  s.stayAwakeWhileCharging = 1;
  CHECK_FALSE(s.preventsAutoSleepWhileCharging(false));
  CHECK(s.preventsAutoSleepWhileCharging(true));

  s.stayAwakeWhileCharging = saved;
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

  s.opdsFilenameFormat = CrossPointSettings::OPDS_FILENAME_FORMAT_COUNT;
  s.autoSyncDayOnBackgroundPing = 7;
  s.validateAndClamp();
  CHECK(s.opdsFilenameFormat == CrossPointSettings::OPDS_FILENAME_AUTHOR_TITLE);
  CHECK(s.autoSyncDayOnBackgroundPing == 1);
}

TEST_CASE("testOpdsFilenameFormatSettingSchema") {
  CrossPointSettings& s = CrossPointSettings::getInstance();
  const auto settings = getSettingsList();
  const SettingInfo* filenameSetting = findSettingByKey(settings, "opdsFilenameFormat");

#if ENABLE_OPDS
  REQUIRE(filenameSetting != nullptr);
  REQUIRE(filenameSetting->dynamicValuesGetter != nullptr);
  REQUIRE(filenameSetting->valueGetter != nullptr);
  REQUIRE(filenameSetting->valueSetter != nullptr);

  const auto labels = filenameSetting->dynamicValuesGetter();
  REQUIRE(labels.size() == CrossPointSettings::OPDS_FILENAME_FORMAT_COUNT);
  CHECK(labels[CrossPointSettings::OPDS_FILENAME_AUTHOR_TITLE] == "Author - Title");
  CHECK(labels[CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR] == "Title - Author");

  s.opdsFilenameFormat = CrossPointSettings::OPDS_FILENAME_AUTHOR_TITLE;
  CHECK(filenameSetting->persistedValue() == CrossPointSettings::OPDS_FILENAME_AUTHOR_TITLE);
  filenameSetting->activateOption(CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR);
  CHECK(s.opdsFilenameFormat == CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR);
#else
  CHECK(filenameSetting == nullptr);
#endif
}

TEST_CASE("testQuickActionClampingAndSettingsWiring") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.shortPwrBtn = CrossPointSettings::TOGGLE_BIONIC_READING;
  s.longPwrBtn = CrossPointSettings::FILE_TRANSFER;
  s.longPressMenuAction = CrossPointSettings::LONG_MENU_TOGGLE_GUIDE_DOTS;
  s.validateAndClamp();

#if ENABLE_FOCUS_READING
  CHECK(s.shortPwrBtn == CrossPointSettings::TOGGLE_BIONIC_READING);
#else
  CHECK(s.shortPwrBtn == CrossPointSettings::IGNORE);
#endif

  // File Transfer (WiFi web server) is always available, so it survives clamping.
  CHECK(s.longPwrBtn == CrossPointSettings::FILE_TRANSFER);

#if ENABLE_GUIDE_DOTS
  CHECK(s.longPressMenuAction == CrossPointSettings::LONG_MENU_TOGGLE_GUIDE_DOTS);
#else
  CHECK(s.longPressMenuAction == CrossPointSettings::LONG_MENU_OFF);
#endif

  s.shortPwrBtn = 255;
  s.longPwrBtn = 255;
  s.longPressMenuAction = 255;
  s.validateAndClamp();
  CHECK(s.shortPwrBtn == CrossPointSettings::IGNORE);
  CHECK(s.longPwrBtn == CrossPointSettings::IGNORE);
  CHECK(s.longPressMenuAction == CrossPointSettings::LONG_MENU_OFF);

  const auto settings = getSettingsList();
  const SettingInfo* shortSetting = findSettingByKey(settings, "shortPwrBtn");
  REQUIRE(shortSetting != nullptr);
  REQUIRE(shortSetting->dynamicValuesGetter != nullptr);
  REQUIRE(shortSetting->valueGetter != nullptr);
  CHECK(!shortSetting->enumPersistedValues.empty());

#if ENABLE_GUIDE_DOTS
  const size_t guideIndex = optionIndexForValue(*shortSetting, CrossPointSettings::TOGGLE_GUIDE_DOTS);
  REQUIRE(guideIndex != shortSetting->enumPersistedValues.size());
  CHECK(guideIndex < shortSetting->enumOptionFeatureKeys.size());
  CHECK(std::string(shortSetting->enumOptionFeatureKeys[guideIndex]) == "guide_dots");
#else
  CHECK(std::find(shortSetting->enumPersistedValues.begin(), shortSetting->enumPersistedValues.end(),
                  CrossPointSettings::TOGGLE_GUIDE_DOTS) == shortSetting->enumPersistedValues.end());
#endif

#if ENABLE_FOCUS_READING
  const size_t bionicIndex = optionIndexForValue(*shortSetting, CrossPointSettings::TOGGLE_BIONIC_READING);
  REQUIRE(bionicIndex != shortSetting->enumPersistedValues.size());
  CHECK(bionicIndex < shortSetting->enumOptionFeatureKeys.size());
  CHECK(std::string(shortSetting->enumOptionFeatureKeys[bionicIndex]) == "focus_reading");
  s.shortPwrBtn = CrossPointSettings::TOGGLE_BIONIC_READING;
  CHECK(shortSetting->valueGetter() == bionicIndex);
#else
  CHECK(std::find(shortSetting->enumPersistedValues.begin(), shortSetting->enumPersistedValues.end(),
                  CrossPointSettings::TOGGLE_BIONIC_READING) == shortSetting->enumPersistedValues.end());
#endif
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

TEST_CASE("testSleepScreenSettingSchemaIsConsistent") {
  const auto settings = getSettingsList();
  const SettingInfo* sleepSetting = findSettingByKey(settings, "sleepScreen");
  REQUIRE(sleepSetting != nullptr);
  REQUIRE(sleepSetting->valueGetter != nullptr);
  REQUIRE(sleepSetting->valueSetter != nullptr);
  REQUIRE(!sleepSetting->enumValues.empty());
  CHECK(sleepSetting->enumValues.size() == sleepSetting->enumPersistedValues.size());
  CHECK(sleepSetting->enumValues.size() == sleepSetting->enumOptionFeatureKeys.size());

  for (const uint8_t persisted : sleepSetting->enumPersistedValues) {
    const size_t index = optionIndexForValue(*sleepSetting, persisted);
    REQUIRE(index != sleepSetting->enumPersistedValues.size());
    CHECK(index < sleepSetting->enumValues.size());
  }
}

TEST_CASE("testSleepScreenEnumCyclesAllPersistedValues") {
  CrossPointSettings& s = CrossPointSettings::getInstance();
  const auto settings = getSettingsList();
  const SettingInfo* sleepSetting = findSettingByKey(settings, "sleepScreen");
  REQUIRE(sleepSetting != nullptr);
  REQUIRE(sleepSetting->valueGetter != nullptr);
  REQUIRE(sleepSetting->valueSetter != nullptr);
  REQUIRE(!sleepSetting->enumPersistedValues.empty());

  const size_t optionCount = sleepSetting->enumPersistedValues.size();
  for (size_t start = 0; start < optionCount; ++start) {
    s.sleepScreen = sleepSetting->enumPersistedValues[start];
    s.sleepScreen = CrossPointSettings::normalizeSleepScreenMode(s.sleepScreen);
    CHECK(sleepSetting->optionPosition() == start);

    for (size_t step = 0; step < optionCount; ++step) {
      const size_t index = sleepSetting->optionPosition();
      REQUIRE(index < optionCount);
      const size_t nextIndex = (index + 1) % optionCount;
      sleepSetting->activateOption(nextIndex);
      s.sleepScreen = CrossPointSettings::normalizeSleepScreenMode(s.sleepScreen);
      CHECK(s.sleepScreen == sleepSetting->enumPersistedValues[nextIndex]);
      CHECK(sleepSetting->persistedValue() == sleepSetting->enumPersistedValues[nextIndex]);
      CHECK(sleepSetting->optionPosition() == nextIndex);
    }
  }
}

TEST_CASE("testSettingInfoDynamicEnumCanonicalValueAccessors") {
  uint8_t persisted = 0;
  const std::vector<uint8_t> values = {0, 1, 4, 2, 3, 7, 8, 13};
  SettingInfo setting =
      SettingInfo::DynamicEnum(
          StrId::STR_NONE_OPT, {},
          [&] {
            const auto it = std::find(values.begin(), values.end(), persisted);
            return it == values.end() ? uint8_t{0} : static_cast<uint8_t>(std::distance(values.begin(), it));
          },
          [&](const uint8_t position) { persisted = values[position]; })
          .withEnumPersistedValues(values);

  persisted = 13;
  CHECK(setting.persistedValue() == 13);
  CHECK(setting.optionPosition() == 7);

  setting.activateOption(2);
  CHECK(persisted == 4);
  CHECK(setting.persistedValue() == 4);
  CHECK(setting.optionPosition() == 2);

  setting.setPersistedValue(7);
  CHECK(persisted == 7);
  CHECK(setting.optionPosition() == 5);
}

TEST_CASE("testVisibleWhenUsesDynamicEnumPersistedValue") {
  uint8_t persisted = 0;
  const std::vector<uint8_t> values = {0, 1, 4, 2, 3, 7, 8, 13};
  SettingInfo anchor =
      SettingInfo::DynamicEnum(
          StrId::STR_NONE_OPT, {},
          [&] {
            const auto it = std::find(values.begin(), values.end(), persisted);
            return it == values.end() ? uint8_t{0} : static_cast<uint8_t>(std::distance(values.begin(), it));
          },
          [&](const uint8_t position) { persisted = values[position]; }, "sleepScreen")
          .withEnumPersistedValues(values);
  SettingInfo dependent = SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::darkMode);
  dependent.withVisibleWhen("sleepScreen", 13);

  const auto visible = [&] { return anchor.persistedValue() == dependent.visibleWhen.eq; };
  anchor.setPersistedValue(8);
  CHECK_FALSE(visible());
  anchor.setPersistedValue(13);
  CHECK(visible());
  anchor.setPersistedValue(0);
  CHECK_FALSE(visible());
}

TEST_CASE("testNormalizeSleepScreenModeClampsUnknownValues") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.sleepScreen = 99;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::DARK);

  // COVER is a first-class UI-selectable mode (sleep split rework); it must
  // survive normalization.
  s.sleepScreen = CrossPointSettings::COVER;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::COVER);

  // SMART is no longer a mode (it became the sleepScreenSplit flag); stored
  // legacy values normalize away.
  s.sleepScreen = CrossPointSettings::SMART;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::DARK);

  // The per-context pickers run through the same normalization.
  s.sleepScreenReader = CrossPointSettings::SMART;
  s.sleepScreenHome = 99;
  s.validateAndClamp();
  CHECK(s.sleepScreenReader == CrossPointSettings::DARK);
  CHECK(s.sleepScreenHome == CrossPointSettings::DARK);

  s.sleepScreenSplit = 7;
  s.validateAndClamp();
  CHECK(s.sleepScreenSplit == CrossPointSettings::SLEEP_SPLIT_UNIFIED);

#if ENABLE_READING_STATS
  s.sleepScreen = CrossPointSettings::READING_STATS_SLEEP;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::READING_STATS_SLEEP);
#endif

#if ENABLE_HAIKU_CLOCK
  s.sleepScreen = CrossPointSettings::HAIKU_CLOCK_SLEEP;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::HAIKU_CLOCK_SLEEP);
#endif

#if ENABLE_NOTES
  s.sleepScreen = CrossPointSettings::NOTES_SLEEP;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::NOTES_SLEEP);
#endif

#if ENABLE_TODO_PLANNER
  s.sleepScreen = CrossPointSettings::PLANNER_SLEEP;
  s.validateAndClamp();
  CHECK(s.sleepScreen == CrossPointSettings::PLANNER_SLEEP);
#endif
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

  CHECK(JsonSettingsIO::loadSettings(s, "{\"sleepScreen\":13}", nullptr));
#if ENABLE_HAIKU_CLOCK
  CHECK(s.sleepScreen == CrossPointSettings::HAIKU_CLOCK_SLEEP);
#else
  CHECK(s.sleepScreen == CrossPointSettings::DARK);
#endif
}

TEST_CASE("testCondensedSettings") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  // Set to defaults/known state
  s.sleepScreen = CrossPointSettings::DARK;
  s.orientation = CrossPointSettings::PORTRAIT;
  s.fontFamily = CrossPointSettings::BOOKERLY;
  s.fontSize = CrossPointSettings::MEDIUM;

  std::string encoded1 = s.getCondensedSettings();
  CHECK(encoded1.length() == 88);  // 64 bytes base64 encoded is 88 chars

  // Changing a setting must change the encoding
  s.sleepScreen = CrossPointSettings::LIGHT;
  std::string encoded2 = s.getCondensedSettings();
  CHECK(encoded2.length() == 88);
  CHECK(encoded1 != encoded2);

  // Changing another setting must change it further
  s.fontSize = CrossPointSettings::LARGE;
  std::string encoded3 = s.getCondensedSettings();
  CHECK(encoded3.length() == 88);
  CHECK(encoded2 != encoded3);
}

TEST_CASE("testSettingsVisibilityEvaluator") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  auto settings = getSettingsList();

  // 1. Test progress bar thickness visibility based on progress bar setting
  const SettingInfo* thicknessSetting = findSettingByKey(settings, "statusBarProgressBarThickness");
  REQUIRE(thicknessSetting != nullptr);
  CHECK(thicknessSetting->visibleWhen.key != nullptr);
  CHECK(std::strcmp(thicknessSetting->visibleWhen.key, "statusBarProgressBar") == 0);
  CHECK(thicknessSetting->visibleWhen.eq == CrossPointSettings::HIDE_PROGRESS);
  CHECK(thicknessSetting->visibleWhen.notEqual == true);

  // 2. Test developer-gated rows (deviceName)
  const SettingInfo* deviceNameSetting = findSettingByKey(settings, "deviceName");
  REQUIRE(deviceNameSetting != nullptr);
  CHECK(deviceNameSetting->visibleWhen.key != nullptr);
  CHECK(std::strcmp(deviceNameSetting->visibleWhen.key, "developerMode") == 0);
  CHECK(deviceNameSetting->visibleWhen.eq == 1);
  CHECK(deviceNameSetting->visibleWhen.notEqual == false);

  // sideButtonLongPress intentionally has NO visibility chain: side and front
  // long-press are independent (EpubReaderActivity picks per fromSideBtn), so
  // gating the side row on longPressButtonBehavior would hide a live setting.
  const SettingInfo* sideLongPressSetting = findSettingByKey(settings, "sideButtonLongPress");
  REQUIRE(sideLongPressSetting != nullptr);
  CHECK(sideLongPressSetting->visibleWhen.key == nullptr);

  // 4. Test side-button orientation-aware visibility based on orientation setting
  const SettingInfo* sideOrientSetting = findSettingByKey(settings, "sideButtonOrientationAware");
  REQUIRE(sideOrientSetting != nullptr);
  CHECK(sideOrientSetting->visibleWhen.key != nullptr);
  CHECK(std::strcmp(sideOrientSetting->visibleWhen.key, "orientation") == 0);
  CHECK(sideOrientSetting->visibleWhen.eq == CrossPointSettings::PORTRAIT);
  CHECK(sideOrientSetting->visibleWhen.notEqual == true);

  // 5. Test front-button orientation-aware visibility based on orientation setting
  const SettingInfo* frontOrientSetting = findSettingByKey(settings, "frontButtonOrientationAware");
  REQUIRE(frontOrientSetting != nullptr);
  CHECK(frontOrientSetting->visibleWhen.key != nullptr);
  CHECK(std::strcmp(frontOrientSetting->visibleWhen.key, "orientation") == 0);
  CHECK(frontOrientSetting->visibleWhen.eq == CrossPointSettings::PORTRAIT);
  CHECK(frontOrientSetting->visibleWhen.notEqual == true);
}

TEST_CASE("forEachSetting category filter never constructs other categories") {
  Storage.reset();
  std::vector<SettingInfo> reader;
  const StrId readerCategory = StrId::STR_CAT_READER;
  forEachSetting(
      [](void* ctx, SettingInfo&& info) { static_cast<std::vector<SettingInfo>*>(ctx)->push_back(std::move(info)); },
      &reader, false, false, buildFontFamilySetting(nullptr), &readerCategory);

  REQUIRE_FALSE(reader.empty());
  for (const auto& setting : reader) {
    CHECK(setting.category == StrId::STR_CAT_READER);
  }
  CHECK(findSettingByKey(reader, "fontSize") != nullptr);
  CHECK(findSettingByKey(reader, "fontFamily") != nullptr);
  CHECK(findSettingByKey(reader, "sleepScreenSplit") == nullptr);
  CHECK(findSettingByKey(reader, "sideButtonLayout") == nullptr);
  CHECK(findSettingByKey(reader, "sleepTimeoutMinutes") == nullptr);

  std::vector<SettingInfo> display;
  const StrId displayCategory = StrId::STR_CAT_DISPLAY;
  forEachSetting(
      [](void* ctx, SettingInfo&& info) { static_cast<std::vector<SettingInfo>*>(ctx)->push_back(std::move(info)); },
      &display, false, false, SettingInfo{}, &displayCategory);
  REQUIRE_FALSE(display.empty());
  for (const auto& setting : display) {
    CHECK(setting.category == StrId::STR_CAT_DISPLAY);
  }
  CHECK(findSettingByKey(display, "sleepScreenSplit") != nullptr);
  CHECK(findSettingByKey(display, "fontSize") == nullptr);
}
