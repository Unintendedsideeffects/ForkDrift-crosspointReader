#pragma once

#include <FeatureFlags.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/settings/SettingsActivity.h"
#include "core/features/FeatureModules.h"

inline bool supportsBackgroundServerModeSetting() {
  return core::FeatureModules::hasCapability(core::Capability::BackgroundServerOnCharge);
}

inline bool supportsBackgroundServerAlwaysSetting() {
  return core::FeatureModules::hasCapability(core::Capability::BackgroundServerAlways);
}

inline std::vector<std::string> backgroundServerModeOptions() {
  std::vector<std::string> options = {std::string(I18N.get(StrId::STR_NEVER)),
                                      std::string(I18N.get(StrId::STR_ONLY_ON_CHARGE))};
  if (supportsBackgroundServerAlwaysSetting()) {
    options.emplace_back(I18N.get(StrId::STR_ALWAYS));
  }
  return options;
}

inline uint8_t getBackgroundServerModeSettingIndex() {
  const uint8_t mode = SETTINGS.getBackgroundServerMode();
  if (supportsBackgroundServerAlwaysSetting()) {
    if (mode == CrossPointSettings::BACKGROUND_SERVER_ALWAYS) {
      return 2;
    }
    if (mode == CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE) {
      return 1;
    }
    return 0;
  }
  return mode == CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE ? 1 : 0;
}

inline void setBackgroundServerModeSettingIndex(const uint8_t index) {
  if (supportsBackgroundServerAlwaysSetting()) {
    SETTINGS.setBackgroundServerMode(
        index <= CrossPointSettings::BACKGROUND_SERVER_ALWAYS ? index : CrossPointSettings::BACKGROUND_SERVER_NEVER);
    return;
  }

  SETTINGS.setBackgroundServerMode(index == 1 ? CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE
                                              : CrossPointSettings::BACKGROUND_SERVER_NEVER);
}

#if ENABLE_WIFI_CLOCK
inline std::vector<std::string> timezoneOffsetOptions() {
  std::vector<std::string> opts;
  opts.reserve(27);
  for (int i = 0; i <= 26; ++i) {
    const int offset = i - 12;
    char buf[8];
    if (offset >= 0) {
      snprintf(buf, sizeof(buf), "UTC+%d", offset);
    } else {
      snprintf(buf, sizeof(buf), "UTC%d", offset);
    }
    opts.emplace_back(buf);
  }
  return opts;
}
#endif

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS, StrId::STR_OPEN_DYSLEXIC};
  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // Reserve: first CrossPointSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  // Capture the SD font count for the lambdas
  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // Total option count = built-in + SD card families
  // For the combined enumStringValues: we need all entries as strings (built-in names + SD names)
  // The render code checks enumStringValues first, then enumValues. So we build enumStringValues
  // with all options when SD fonts are present.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0 || hasUserFonts) {
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SERIF));
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.push_back(I18N.get(StrId::STR_OPEN_DYSLEXIC));
    if (hasUserFonts) {
      allStringValues.push_back(I18N.get(StrId::STR_EXTERNAL_FONT));
    }
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames, hasUserFonts]() -> uint8_t {
    if (hasUserFonts && SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
      return CrossPointSettings::BUILTIN_FONT_COUNT;
    }

    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0) + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames, hasUserFonts](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else if (hasUserFonts && v == CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = CrossPointSettings::USER_SD;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      const int sdBase = CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0);
      int sdIdx = v - sdBase;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
        if (SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
          SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
        }
      }
    }
  };

  return s;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr) {
  // IMPORTANT: do NOT use brace-initialization here.
  // std::initializer_list<SettingInfo> backs all N elements as a temporary stack array simultaneously.
  // SettingInfo is ~200 bytes (five std::function members); 32+ entries × 200B = ~6.4KB stack spike
  // that overflows the 8KB loopTask stack. push_back constructs one entry at a time on the stack,
  // immediately move-constructs it into the heap vector buffer, then destroys the temporary.
  std::vector<SettingInfo> list;
  list.reserve(48);  // Upper-bound estimate; prevents repeated 2× heap reallocation at low heap

  // --- Display ---
  // Sleep screen uses DynamicEnum with explicit value mapping so display order
  // (Dark, Light, Follow Theme, Custom, Transparent, Smart) is independent of the
  // SLEEP_SCREEN_MODE enum values (DARK=0, LIGHT=1, CUSTOM=2, TRANSPARENT=3, FOLLOW_THEME=4, SMART=7).
  list.push_back([] {
    using M = CrossPointSettings::SLEEP_SCREEN_MODE;
    const std::vector<StrId> ids = {StrId::STR_DARK,   StrId::STR_LIGHT,       StrId::STR_FOLLOW_THEME,
                                    StrId::STR_CUSTOM, StrId::STR_TRANSPARENT, StrId::STR_SLEEP_SMART};
    const std::vector<uint8_t> vals = {M::DARK, M::LIGHT, M::FOLLOW_THEME, M::CUSTOM, M::TRANSPARENT, M::SMART};
    return SettingInfo::DynamicEnum(
        StrId::STR_SLEEP_SCREEN, ids,
        [vals] {
          const uint8_t cur = SETTINGS.sleepScreen;
          for (size_t i = 0; i < vals.size(); i++) {
            if (vals[i] == cur) return static_cast<uint8_t>(i);
          }
          return uint8_t{0};
        },
        [vals](uint8_t idx) {
          if (idx < vals.size()) SETTINGS.sleepScreen = vals[idx];
        },
        "sleepScreen", StrId::STR_CAT_DISPLAY);
  }());
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_SOURCE, &CrossPointSettings::sleepScreenSource,
                                   {StrId::STR_SLEEP, StrId::STR_POKEDEX, StrId::STR_ALL}, "sleepScreenSource",
                                   StrId::STR_CAT_DISPLAY)
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                                   {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY)
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                                   {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                                   "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY)
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_CYCLE_MODE, &CrossPointSettings::sleepCycleMode,
                                   {StrId::STR_RANDOM, StrId::STR_SEQUENTIAL}, "sleepCycleMode", StrId::STR_CAT_DISPLAY)
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Action(StrId::STR_VALIDATE_SLEEP_IMAGES, SettingAction::ValidateSleepImages));
  list.push_back(SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                                     "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR));
  list.push_back(SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                     &CrossPointSettings::statusBarBookProgressPercentage,
                                     "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR));
  list.push_back(SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                                   {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                                   StrId::STR_CUSTOMISE_STATUS_BAR));
  list.push_back(
      SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                        {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                        "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR));
  list.push_back(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                                   {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                                   StrId::STR_CUSTOMISE_STATUS_BAR));
  list.push_back(SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                                     StrId::STR_CUSTOMISE_STATUS_BAR));
  list.push_back(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                                   {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                                   StrId::STR_CAT_DISPLAY));
  list.push_back(SettingInfo::Enum(
      StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
      {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
      "refreshFrequency", StrId::STR_CAT_DISPLAY));
  // Build options with explicit enum-value mapping so position != value assumptions
  // don't break when individual themes are optionally included or excluded.
  list.push_back([] {
    std::vector<StrId> ids = {StrId::STR_THEME_CLASSIC};
    std::vector<uint8_t> vals = {CrossPointSettings::UI_THEME::CLASSIC};
    if (core::FeatureModules::hasCapability(core::Capability::LyraTheme)) {
      ids.insert(ids.end(), {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED, StrId::STR_THEME_FORK_DRIFT,
                             StrId::STR_THEME_MINIMAL});
      vals.insert(vals.end(), {CrossPointSettings::UI_THEME::LYRA, CrossPointSettings::UI_THEME::LYRA_EXTENDED,
                               CrossPointSettings::UI_THEME::FORK_DRIFT, CrossPointSettings::UI_THEME::MINIMAL});
    }
    if (core::FeatureModules::hasCapability(core::Capability::PokemonParty)) {
      ids.push_back(StrId::STR_THEME_POKEMON_PARTY);
      vals.push_back(CrossPointSettings::UI_THEME::POKEMON_PARTY);
    }
    return SettingInfo::DynamicEnum(
        StrId::STR_UI_THEME, std::move(ids),
        [vals] {
          const uint8_t cur = SETTINGS.uiTheme;
          for (size_t i = 0; i < vals.size(); i++) {
            if (vals[i] == cur) return static_cast<uint8_t>(i);
          }
          return uint8_t{0};
        },
        [vals](uint8_t idx) {
          if (idx < vals.size()) SETTINGS.uiTheme = vals[idx];
        },
        "uiTheme", StrId::STR_CAT_DISPLAY);
  }());
  list.push_back(SettingInfo::Enum(StrId::STR_RECENT_BOOKS_VIEW, &CrossPointSettings::recentBooksView,
                                   {StrId::STR_LIST_VIEW, StrId::STR_GRID_VIEW}, "recentBooksView",
                                   StrId::STR_CAT_DISPLAY));
  list.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                                     StrId::STR_CAT_DISPLAY));

  // --- Reader ---
  list.push_back(buildFontFamilySetting(registry));
  list.push_back(SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                                   {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE},
                                   "fontSize", StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                                   {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing",
                                   StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5},
                                    "screenMargin", StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Enum(
      StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
      {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE},
      "paragraphAlignment", StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                                     StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled,
                                     "hyphenationEnabled", StrId::STR_CAT_READER));
  list.push_back(
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                        "orientation", StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                                     "extraParagraphSpacing", StrId::STR_CAT_READER));
  list.push_back(SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                                     StrId::STR_CAT_READER));
  list.push_back(
      SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                        "imageRendering", StrId::STR_CAT_READER));

  // --- Controls ---
  list.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                   {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}, "sideButtonLayout",
                                   StrId::STR_CAT_CONTROLS));
  list.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                                   {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                    StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                                   "longPressButtonBehavior", StrId::STR_CAT_CONTROLS));
  list.push_back(SettingInfo::Enum(
      StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
      {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_SELECT, StrId::STR_FORCE_REFRESH},
      "shortPwrBtn", StrId::STR_CAT_CONTROLS));

  // --- System ---
  list.push_back(SettingInfo::Value(
      StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
      {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
      "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));
  list.push_back(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles,
                                     "showHiddenFiles", StrId::STR_CAT_SYSTEM));
  list.push_back(SettingInfo::Toggle(StrId::STR_DEVELOPER_MODE, &CrossPointSettings::developerMode, "developerMode",
                                     StrId::STR_CAT_SYSTEM));

  if (core::FeatureModules::hasCapability(core::Capability::TrmnlSwitch)) {
    list.push_back(SettingInfo::Action(StrId::STR_SWITCH_TO_TRMNL, SettingAction::SwitchToTrmnl));
  }

  if (core::FeatureModules::hasCapability(core::Capability::DarkMode)) {
    list.push_back(
        SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_DISPLAY));
  }

  if (core::FeatureModules::hasCapability(core::Capability::GlobalStatusBar)) {
    list.push_back(SettingInfo::Enum(StrId::STR_GLOBAL_STATUS_BAR, &CrossPointSettings::globalStatusBar,
                                     {StrId::STR_OFF, StrId::STR_ON, StrId::STR_NO_SLEEP}, "globalStatusBar",
                                     StrId::STR_CAT_DISPLAY));
    list.push_back(SettingInfo::Enum(StrId::STR_STATUS_BAR_POSITION, &CrossPointSettings::globalStatusBarPosition,
                                     {StrId::STR_STATUS_BAR_TOP, StrId::STR_STATUS_BAR_BOTTOM},
                                     "globalStatusBarPosition", StrId::STR_CAT_DISPLAY));
  }

  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    list.push_back(SettingInfo::Toggle(StrId::STR_FILE_TRANSFER, &CrossPointSettings::usbMscPromptOnConnect,
                                       "usbMscPromptOnConnect", StrId::STR_CAT_SYSTEM));
  }

  if (supportsBackgroundServerModeSetting()) {
    list.push_back(SettingInfo::DynamicEnum(
        StrId::STR_BACKGROUND_SERVER, {}, [] { return getBackgroundServerModeSettingIndex(); },
        [](uint8_t value) { setBackgroundServerModeSettingIndex(value); }, "backgroundServerMode",
        StrId::STR_CAT_SYSTEM, [] { return backgroundServerModeOptions(); }));
  }

  // Device name for mDNS/DHCP/AP SSID. Editable on-device via keyboard (STRING handler).
  // Input is sanitized to [a-z0-9-], max 24 chars, via validateAndClamp() on save.
  list.push_back(SettingInfo::String(StrId::STR_DEVICE_NAME, SETTINGS.deviceName, sizeof(SETTINGS.deviceName),
                                     "deviceName", StrId::STR_CAT_SYSTEM));

#if ENABLE_WIFI_CLOCK
  list.push_back(SettingInfo::Enum(StrId::STR_TIME_MODE, &CrossPointSettings::timeMode,
                                   {StrId::STR_TIME_UTC, StrId::STR_TIME_LOCAL, StrId::STR_TIME_MANUAL}, "timeMode",
                                   StrId::STR_CAT_TIME));
  list.push_back(SettingInfo::DynamicEnum(
                     StrId::STR_TIMEZONE_OFFSET, {}, [] { return SETTINGS.timeZoneOffset; },
                     [](uint8_t v) { SETTINGS.timeZoneOffset = std::min(v, uint8_t{26}); }, "timeZoneOffset",
                     StrId::STR_CAT_TIME, timezoneOffsetOptions)
                     .withVisibleWhen("timeMode", CrossPointSettings::TIME_MODE_LOCAL));
#endif

  if (core::FeatureModules::hasCapability(core::Capability::KoreaderSync)) {
    // --- KOReader Sync (web-only, persisted via FeatureModules) ---
    list.push_back(SettingInfo::DynamicString(
        StrId::STR_KOREADER_USERNAME, [] { return core::FeatureModules::getKoreaderUsername(); },
        [](const std::string& value) { core::FeatureModules::setKoreaderUsername(value, false); }, "koUsername",
        StrId::STR_KOREADER_SYNC));
    list.push_back(SettingInfo::DynamicString(
        StrId::STR_KOREADER_PASSWORD, [] { return core::FeatureModules::getKoreaderPassword(); },
        [](const std::string& value) { core::FeatureModules::setKoreaderPassword(value, false); }, "koPassword",
        StrId::STR_KOREADER_SYNC));
    list.push_back(SettingInfo::DynamicString(
        StrId::STR_SYNC_SERVER_URL, [] { return core::FeatureModules::getKoreaderServerUrl(); },
        [](const std::string& value) { core::FeatureModules::setKoreaderServerUrl(value, false); }, "koServerUrl",
        StrId::STR_KOREADER_SYNC));
    list.push_back(SettingInfo::DynamicEnum(
        StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
        [] { return core::FeatureModules::getKoreaderMatchMethod(); },
        [](uint8_t value) { core::FeatureModules::setKoreaderMatchMethod(value, false); }, "koMatchMethod",
        StrId::STR_KOREADER_SYNC));
  }

  if (core::FeatureModules::hasCapability(core::Capability::UserFonts)) {
    list.push_back(SettingInfo::DynamicEnum(
        StrId::STR_EXTERNAL_FONT, {}, [] { return core::FeatureModules::getSelectedUserFontFamilyIndex(); },
        [](uint8_t value) { core::FeatureModules::setSelectedUserFontFamilyIndex(value); }, "userFontPath",
        StrId::STR_CAT_READER, [] { return core::FeatureModules::getUserFontFamilies(); }));
  }

  if (core::FeatureModules::hasCapability(core::Capability::CalibreSync)) {
    // OPDS intentionally binds directly to SETTINGS char arrays because SettingInfo::String
    // edits in-place mutable storage; unlike KOReader credentials, OPDS persistence remains
    // owned by CrossPointSettings/JsonSettingsIO.
    list.push_back(SettingInfo::String(StrId::STR_OPDS_SERVER_URL, SETTINGS.opdsServerUrl,
                                       sizeof(SETTINGS.opdsServerUrl), "opdsServerUrl", StrId::STR_OPDS_BROWSER));
    list.push_back(SettingInfo::String(StrId::STR_USERNAME, SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername),
                                       "opdsUsername", StrId::STR_OPDS_BROWSER));
    list.push_back(SettingInfo::String(StrId::STR_PASSWORD, SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword),
                                       "opdsPassword", StrId::STR_OPDS_BROWSER)
                       .withObfuscated());
  }

  return list;
}
