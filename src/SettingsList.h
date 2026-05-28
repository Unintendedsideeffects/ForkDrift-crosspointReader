#pragma once

#include <FeatureFlags.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "SettingInfo.h"
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

struct QuickActionOption {
  StrId label;
  uint8_t value;
  const char* featureKey = nullptr;
};

inline std::vector<std::string> quickActionOptionLabels(const std::vector<QuickActionOption>& options) {
  std::vector<std::string> labels;
  labels.reserve(options.size());
  for (const auto& option : options) {
    labels.emplace_back(I18N.get(option.label));  // cppcheck-suppress useStlAlgorithm
  }
  return labels;
}

inline std::vector<uint8_t> quickActionPersistedValues(const std::vector<QuickActionOption>& options) {
  std::vector<uint8_t> values;
  values.reserve(options.size());
  for (const auto& option : options) {
    values.push_back(option.value);  // cppcheck-suppress useStlAlgorithm
  }
  return values;
}

inline std::vector<const char*> quickActionFeatureKeys(const std::vector<QuickActionOption>& options) {
  std::vector<const char*> featureKeys;
  featureKeys.reserve(options.size());
  for (const auto& option : options) {
    featureKeys.push_back(option.featureKey);  // cppcheck-suppress useStlAlgorithm
  }
  return featureKeys;
}

inline uint8_t quickActionOptionIndex(const std::vector<QuickActionOption>& options, const uint8_t value) {
  for (size_t i = 0; i < options.size(); ++i) {
    if (options[i].value == value) {
      return static_cast<uint8_t>(i);
    }
  }
  return 0;
}

inline uint8_t quickActionValueForIndex(const std::vector<QuickActionOption>& options, const uint8_t index,
                                        const uint8_t fallbackValue) {
  if (index < options.size()) {
    return options[index].value;
  }
  return fallbackValue;
}

inline std::vector<QuickActionOption> shortPowerButtonOptions() {
  using S = CrossPointSettings;
  std::vector<QuickActionOption> options = {
      {StrId::STR_IGNORE, S::IGNORE},
      {StrId::STR_SLEEP, S::SLEEP},
      {StrId::STR_PAGE_TURN, S::PAGE_TURN},
      {StrId::STR_SELECT, S::SELECT},
      {StrId::STR_FORCE_REFRESH, S::FORCE_REFRESH},
      {StrId::STR_CHANGE_FONT, S::TOGGLE_FONT},
  };
  if (core::FeatureModules::hasCapability(core::Capability::GuideDots)) {
    options.push_back({StrId::STR_TOGGLE_GUIDE_DOTS, S::TOGGLE_GUIDE_DOTS, "guide_dots"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::FocusReading)) {
    options.push_back({StrId::STR_TOGGLE_BIONIC_READING, S::TOGGLE_BIONIC_READING, "focus_reading"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::Bookmarks)) {
    options.push_back({StrId::STR_TOGGLE_BOOKMARK, S::TOGGLE_BOOKMARK, "bookmarks"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::KoreaderSync)) {
    options.push_back({StrId::STR_SYNC_PROGRESS, S::SYNC_PROGRESS, "koreader_sync"});
  }
#if ENABLE_READING_STATS
  options.push_back({StrId::STR_MARK_FINISHED, S::MARK_FINISHED, "reading_stats"});
  options.push_back({StrId::STR_READING_STATS, S::READING_STATS, "reading_stats"});
#endif
  options.push_back({StrId::STR_SCREENSHOT_BUTTON, S::SCREENSHOT});
  options.push_back({StrId::STR_CYCLE_PAGE_TURN, S::CYCLE_PAGE_TURN});
  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    options.push_back({StrId::STR_FILE_TRANSFER, S::FILE_TRANSFER, "usb_mass_storage"});
  }
  return options;
}

inline std::vector<QuickActionOption> longPowerButtonOptions() {
  using S = CrossPointSettings;
  std::vector<QuickActionOption> options = {
      {StrId::STR_IGNORE, S::IGNORE},           {StrId::STR_SLEEP, S::SLEEP},
      {StrId::STR_PAGE_TURN, S::PAGE_TURN},     {StrId::STR_FORCE_REFRESH, S::FORCE_REFRESH},
      {StrId::STR_CHANGE_FONT, S::TOGGLE_FONT},
  };
  if (core::FeatureModules::hasCapability(core::Capability::GuideDots)) {
    options.push_back({StrId::STR_TOGGLE_GUIDE_DOTS, S::TOGGLE_GUIDE_DOTS, "guide_dots"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::FocusReading)) {
    options.push_back({StrId::STR_TOGGLE_BIONIC_READING, S::TOGGLE_BIONIC_READING, "focus_reading"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::Bookmarks)) {
    options.push_back({StrId::STR_TOGGLE_BOOKMARK, S::TOGGLE_BOOKMARK, "bookmarks"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::KoreaderSync)) {
    options.push_back({StrId::STR_SYNC_PROGRESS, S::SYNC_PROGRESS, "koreader_sync"});
  }
#if ENABLE_READING_STATS
  options.push_back({StrId::STR_MARK_FINISHED, S::MARK_FINISHED, "reading_stats"});
  options.push_back({StrId::STR_READING_STATS, S::READING_STATS, "reading_stats"});
#endif
  options.push_back({StrId::STR_SCREENSHOT_BUTTON, S::SCREENSHOT});
  options.push_back({StrId::STR_CYCLE_PAGE_TURN, S::CYCLE_PAGE_TURN});
  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    options.push_back({StrId::STR_FILE_TRANSFER, S::FILE_TRANSFER, "usb_mass_storage"});
  }
  return options;
}

inline std::vector<QuickActionOption> longPressMenuActionOptions() {
  using S = CrossPointSettings;
  std::vector<QuickActionOption> options = {
      {StrId::STR_IGNORE, S::LONG_MENU_OFF},
      {StrId::STR_SLEEP, S::LONG_MENU_SLEEP},
      {StrId::STR_CHANGE_FONT, S::LONG_MENU_CHANGE_FONT},
  };
  if (core::FeatureModules::hasCapability(core::Capability::GuideDots)) {
    options.push_back({StrId::STR_TOGGLE_GUIDE_DOTS, S::LONG_MENU_TOGGLE_GUIDE_DOTS, "guide_dots"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::FocusReading)) {
    options.push_back({StrId::STR_TOGGLE_BIONIC_READING, S::LONG_MENU_TOGGLE_BIONIC, "focus_reading"});
  }
  if (core::FeatureModules::hasCapability(core::Capability::Bookmarks)) {
    options.push_back({StrId::STR_TOGGLE_BOOKMARK, S::LONG_MENU_TOGGLE_BOOKMARK, "bookmarks"});
  }
  options.push_back({StrId::STR_FORCE_REFRESH, S::LONG_MENU_REFRESH_SCREEN});
  if (core::FeatureModules::hasCapability(core::Capability::KoreaderSync)) {
    options.push_back({StrId::STR_SYNC_PROGRESS, S::LONG_MENU_SYNC_PROGRESS, "koreader_sync"});
  }
#if ENABLE_READING_STATS
  options.push_back({StrId::STR_MARK_FINISHED, S::LONG_MENU_MARK_FINISHED, "reading_stats"});
  options.push_back({StrId::STR_READING_STATS, S::LONG_MENU_READING_STATS, "reading_stats"});
#endif
  options.push_back({StrId::STR_SCREENSHOT_BUTTON, S::LONG_MENU_SCREENSHOT});
  options.push_back({StrId::STR_CYCLE_PAGE_TURN, S::LONG_MENU_CYCLE_PAGE_TURN});
  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    options.push_back({StrId::STR_FILE_TRANSFER, S::LONG_MENU_FILE_TRANSFER, "usb_mass_storage"});
  }
  return options;
}

#if ENABLE_WIFI_CLOCK
inline std::vector<std::string> timezoneOffsetOptions() {
  static const char* kCities[] = {
      "Baker Island",   // UTC-12
      "Pago Pago",      // UTC-11
      "Honolulu",       // UTC-10
      "Anchorage",      // UTC-9
      "Los Angeles",    // UTC-8
      "Denver",         // UTC-7
      "Chicago",        // UTC-6
      "New York",       // UTC-5
      "Santiago",       // UTC-4
      "Buenos Aires",   // UTC-3
      "South Georgia",  // UTC-2
      "Azores",         // UTC-1
      "London",         // UTC+0
      "Paris",          // UTC+1
      "Cairo",          // UTC+2
      "Moscow",         // UTC+3
      "Dubai",          // UTC+4
      "Karachi",        // UTC+5
      "Dhaka",          // UTC+6
      "Bangkok",        // UTC+7
      "Shanghai",       // UTC+8
      "Tokyo",          // UTC+9
      "Sydney",         // UTC+10
      "Noumea",         // UTC+11
      "Auckland",       // UTC+12
      "Nuku'alofa",     // UTC+13
      "Kiritimati"      // UTC+14
  };

  std::vector<std::string> opts;
  opts.reserve(27);
  for (int i = 0; i <= 26; ++i) {
    const int offset = i - 12;
    char buf[64];
    if (offset >= 0) {
      snprintf(buf, sizeof(buf), "UTC+%d (%s)", offset, kCities[i]);
    } else {
      snprintf(buf, sizeof(buf), "UTC%d (%s)", offset, kCities[i]);
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

  std::vector<const char*> optionFeatureKeys = {"bookerly_fonts", "notosans_fonts", "opendyslexic_fonts"};
  if (hasUserFonts) {
    optionFeatureKeys.push_back("user_fonts");
  }

  s.withConfiguratorExport().withEnumOptionFeatureKeys(std::move(optionFeatureKeys));
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
  // stays independent of the persisted enum values.
  list.push_back([] {
    using M = CrossPointSettings::SLEEP_SCREEN_MODE;
    const std::vector<StrId> ids = {
        StrId::STR_DARK,          StrId::STR_LIGHT,       StrId::STR_FOLLOW_THEME,
        StrId::STR_CUSTOM,        StrId::STR_TRANSPARENT, StrId::STR_SLEEP_SMART,
#if ENABLE_ROMAN_CLOCK_SLEEP
        StrId::STR_ROMAN_CLOCK,
#endif
#if ENABLE_HAIKU_CLOCK
        StrId::STR_HAIKU_CLOCK,
#endif
#if ENABLE_READING_STATS
        StrId::STR_READING_STATS,
#endif
    };
    const std::vector<uint8_t> vals = {
        M::DARK,
        M::LIGHT,
        M::FOLLOW_THEME,
        M::CUSTOM,
        M::TRANSPARENT,
        M::SMART,
#if ENABLE_ROMAN_CLOCK_SLEEP
        M::ROMAN_CLOCK_SLEEP,
#endif
#if ENABLE_HAIKU_CLOCK
        M::HAIKU_CLOCK_SLEEP,
#endif
#if ENABLE_READING_STATS
        M::READING_STATS_SLEEP,
#endif
    };
    std::vector<const char*> optionFeatureKeys = {
        nullptr,
        nullptr,
        nullptr,
        "image_sleep",
        nullptr,
        nullptr,
#if ENABLE_ROMAN_CLOCK_SLEEP
        "roman_clock_sleep",
#endif
#if ENABLE_HAIKU_CLOCK
        "haiku_clock_sleep",
#endif
#if ENABLE_READING_STATS
        "reading_stats",
#endif
    };
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
               "sleepScreen", StrId::STR_CAT_DISPLAY)
        .withConfiguratorExport()
        .withEnumPersistedValues(vals)
        .withEnumOptionFeatureKeys(std::move(optionFeatureKeys));
  }());
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_SOURCE, &CrossPointSettings::sleepScreenSource,
                                   {StrId::STR_SLEEP, StrId::STR_POKEDEX, StrId::STR_ALL}, "sleepScreenSource",
                                   StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport()
                     .withEnumOptionFeatureKeys({nullptr, "pokemon_party", "pokemon_party"})
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                                   {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport()
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                                   {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                                   "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport()
                     .withVisibleWhen("sleepScreen", 3));
  list.push_back(SettingInfo::Enum(StrId::STR_SLEEP_CYCLE_MODE, &CrossPointSettings::sleepCycleMode,
                                   {StrId::STR_RANDOM, StrId::STR_SEQUENTIAL}, "sleepCycleMode", StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport()
                     .withVisibleWhen("sleepScreen", 3));
#if ENABLE_HAIKU_CLOCK
  list.push_back(SettingInfo::Toggle(StrId::STR_HAIKU_CLOCK_LANDSCAPE, &CrossPointSettings::haikuClockLandscape,
                                     "haikuClockLandscape", StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport()
                     .withVisibleWhen("sleepScreen", CrossPointSettings::HAIKU_CLOCK_SLEEP));
#endif
  list.push_back(SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                                     "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                     &CrossPointSettings::statusBarBookProgressPercentage,
                                     "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                                   {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                                   StrId::STR_CUSTOMISE_STATUS_BAR)
                     .withConfiguratorExport());
  list.push_back(
      SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                        {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                        "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR)
          .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                                   {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                                   StrId::STR_CUSTOMISE_STATUS_BAR)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                                     StrId::STR_CUSTOMISE_STATUS_BAR)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                                   {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                                   StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                                   {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                                    StrId::STR_PAGES_30},
                                   "refreshFrequency", StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport());
  // Build options with explicit enum-value mapping so position != value assumptions
  // don't break when individual themes are optionally included or excluded.
  list.push_back([] {
    std::vector<StrId> ids = {StrId::STR_THEME_CLASSIC};
    std::vector<uint8_t> vals = {CrossPointSettings::UI_THEME::CLASSIC};
    std::vector<const char*> optionFeatureKeys = {nullptr};
    if (core::FeatureModules::hasCapability(core::Capability::LyraTheme)) {
      ids.insert(ids.end(), {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED, StrId::STR_THEME_FORK_DRIFT,
                             StrId::STR_THEME_LYRA_CAROUSEL});
      vals.insert(vals.end(), {CrossPointSettings::UI_THEME::LYRA, CrossPointSettings::UI_THEME::LYRA_EXTENDED,
                               CrossPointSettings::UI_THEME::FORK_DRIFT, CrossPointSettings::UI_THEME::LYRA_CAROUSEL});
      optionFeatureKeys.insert(optionFeatureKeys.end(), {"lyra_theme", "lyra_theme", "lyra_theme", "lyra_theme"});
      if (core::FeatureModules::hasCapability(core::Capability::MinimalTheme)) {
        ids.push_back(StrId::STR_THEME_MINIMAL);
        vals.push_back(CrossPointSettings::UI_THEME::MINIMAL);
        optionFeatureKeys.push_back("minimal_theme");
      }
    }
    if (core::FeatureModules::hasCapability(core::Capability::PokemonParty)) {
      ids.push_back(StrId::STR_THEME_POKEMON_PARTY);
      vals.push_back(CrossPointSettings::UI_THEME::POKEMON_PARTY);
      optionFeatureKeys.push_back("pokemon_party");
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
               "uiTheme", StrId::STR_CAT_DISPLAY)
        .withConfiguratorExport()
        .withEnumPersistedValues(vals)
        .withEnumOptionFeatureKeys(std::move(optionFeatureKeys));
  }());
  list.push_back(SettingInfo::Enum(StrId::STR_RECENT_BOOKS_VIEW, &CrossPointSettings::recentBooksView,
                                   {StrId::STR_LIST_VIEW, StrId::STR_GRID_VIEW}, "recentBooksView",
                                   StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                                     StrId::STR_CAT_DISPLAY)
                     .withConfiguratorExport());

  // --- Reader ---
  list.push_back(buildFontFamilySetting(registry));
  list.push_back(SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                                   {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE},
                                   "fontSize", StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                                   {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing",
                                   StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5},
                                    "screenMargin", StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                                   {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER,
                                    StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE},
                                   "paragraphAlignment", StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                                     StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  if (core::FeatureModules::hasCapability(core::Capability::FocusReading)) {
    list.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                       "focusReadingEnabled", StrId::STR_CAT_READER)
                       .withConfiguratorExport("focus_reading"));
  }
  if (core::FeatureModules::hasCapability(core::Capability::GuideDots)) {
    list.push_back(SettingInfo::Toggle(StrId::STR_GUIDE_READING, &CrossPointSettings::guideReadingEnabled,
                                       "guideReadingEnabled", StrId::STR_CAT_READER)
                       .withConfiguratorExport("guide_dots"));
  }
  list.push_back(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled,
                                     "hyphenationEnabled", StrId::STR_CAT_READER)
                     .withConfiguratorExport("hyphenation"));
  list.push_back(
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                        "orientation", StrId::STR_CAT_READER)
          .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                                     "extraParagraphSpacing", StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_FORCE_PARAGRAPH_INDENTS, &CrossPointSettings::forceParagraphIndents,
                                     "forceParagraphIndents", StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                                     StrId::STR_CAT_READER)
                     .withConfiguratorExport());
  list.push_back(
      SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                        {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                        "imageRendering", StrId::STR_CAT_READER)
          .withConfiguratorExport("book_images"));

  // --- Controls ---
  list.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                   {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV}, "sideButtonLayout",
                                   StrId::STR_CAT_CONTROLS)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_ORIENTATION_AWARE, &CrossPointSettings::sideButtonOrientationAware,
                                   {StrId::STR_NO, StrId::STR_YES}, "sideButtonOrientationAware",
                                   StrId::STR_CAT_CONTROLS)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LONG_PRESS, &CrossPointSettings::sideButtonLongPress,
                                   {StrId::STR_CHAPTER_SKIP_OPT, StrId::STR_CHANGE_FONT_SIZE, StrId::STR_OFF},
                                   "sideButtonLongPress", StrId::STR_CAT_CONTROLS)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_ORIENTATION_AWARE, &CrossPointSettings::frontButtonOrientationAware,
                                   {StrId::STR_NO, StrId::STR_NAV_BUTTONS, StrId::STR_ALL_BUTTONS},
                                   "frontButtonOrientationAware", StrId::STR_CAT_CONTROLS)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                                   {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                    StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                                   "longPressButtonBehavior", StrId::STR_CAT_CONTROLS)
                     .withConfiguratorExport());
  list.push_back([] {
    const auto options = shortPowerButtonOptions();
    SettingInfo setting = SettingInfo::DynamicEnum(
        StrId::STR_SHORT_PWR_BTN, {}, [options] { return quickActionOptionIndex(options, SETTINGS.shortPwrBtn); },
        [options](uint8_t index) {
          SETTINGS.shortPwrBtn = quickActionValueForIndex(options, index, CrossPointSettings::IGNORE);
        },
        "shortPwrBtn", StrId::STR_CAT_CONTROLS, [options] { return quickActionOptionLabels(options); });
    setting.withConfiguratorExport()
        .withEnumPersistedValues(quickActionPersistedValues(options))
        .withEnumOptionFeatureKeys(quickActionFeatureKeys(options));
    return setting;
  }());
  list.push_back([] {
    const auto options = longPowerButtonOptions();
    SettingInfo setting = SettingInfo::DynamicEnum(
        StrId::STR_LONG_PRESS_ACTION, {}, [options] { return quickActionOptionIndex(options, SETTINGS.longPwrBtn); },
        [options](uint8_t index) {
          SETTINGS.longPwrBtn = quickActionValueForIndex(options, index, CrossPointSettings::IGNORE);
        },
        "longPwrBtn", StrId::STR_CAT_CONTROLS, [options] { return quickActionOptionLabels(options); });
    setting.withConfiguratorExport()
        .withEnumPersistedValues(quickActionPersistedValues(options))
        .withEnumOptionFeatureKeys(quickActionFeatureKeys(options));
    return setting;
  }());
  list.push_back([] {
    const auto options = longPressMenuActionOptions();
    SettingInfo setting = SettingInfo::DynamicEnum(
        StrId::STR_LONG_PRESS_MENU_ACTION, {},
        [options] { return quickActionOptionIndex(options, SETTINGS.longPressMenuAction); },
        [options](uint8_t index) {
          SETTINGS.longPressMenuAction = quickActionValueForIndex(options, index, CrossPointSettings::LONG_MENU_OFF);
        },
        "longPressMenuAction", StrId::STR_CAT_CONTROLS, [options] { return quickActionOptionLabels(options); });
    setting.withConfiguratorExport()
        .withEnumPersistedValues(quickActionPersistedValues(options))
        .withEnumOptionFeatureKeys(quickActionFeatureKeys(options));
    return setting;
  }());

  // --- System ---
  list.push_back(SettingInfo::Value(
                     StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
                     {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
                     "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles,
                                     "showHiddenFiles", StrId::STR_CAT_SYSTEM)
                     .withConfiguratorExport());
#if ENABLE_TODO_PLANNER
  if (core::FeatureModules::hasCapability(core::Capability::TodoPlanner)) {
    list.push_back(SettingInfo::Toggle(StrId::STR_TODO_OPEN_DIRECT_TO_TODAY, &CrossPointSettings::todoOpenDirectToToday,
                                       "todoOpenDirectToToday", StrId::STR_CAT_SYSTEM)
                       .withConfiguratorExport("todo_planner"));
  }
#endif
#if ENABLE_READING_STATS
  list.push_back(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                                     "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM)
                     .withConfiguratorExport("reading_stats"));
#endif

  if (core::FeatureModules::hasCapability(core::Capability::TrmnlSwitch)) {
    list.push_back(SettingInfo::Action(StrId::STR_SWITCH_TO_TRMNL, SettingAction::SwitchToTrmnl));
  }

  if (core::FeatureModules::hasCapability(core::Capability::DarkMode)) {
    list.push_back(
        SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_DISPLAY)
            .withConfiguratorExport("dark_mode"));
  }

  if (core::FeatureModules::hasCapability(core::Capability::GlobalStatusBar)) {
    list.push_back(SettingInfo::Enum(StrId::STR_GLOBAL_STATUS_BAR, &CrossPointSettings::globalStatusBar,
                                     {StrId::STR_OFF, StrId::STR_ON}, "globalStatusBar", StrId::STR_CAT_DISPLAY)
                       .withConfiguratorExport("global_status_bar"));
    list.push_back(SettingInfo::Enum(StrId::STR_STATUS_BAR_POSITION, &CrossPointSettings::globalStatusBarPosition,
                                     {StrId::STR_STATUS_BAR_TOP, StrId::STR_STATUS_BAR_BOTTOM},
                                     "globalStatusBarPosition", StrId::STR_CAT_DISPLAY)
                       .withConfiguratorExport("global_status_bar"));
  }

  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    list.push_back(SettingInfo::Toggle(StrId::STR_FILE_TRANSFER, &CrossPointSettings::usbMscPromptOnConnect,
                                       "usbMscPromptOnConnect", StrId::STR_CAT_SYSTEM)
                       .withConfiguratorExport("usb_mass_storage"));
  }

  if (supportsBackgroundServerModeSetting()) {
    list.push_back(SettingInfo::DynamicEnum(
                       StrId::STR_BACKGROUND_SERVER, {}, [] { return getBackgroundServerModeSettingIndex(); },
                       [](uint8_t value) { setBackgroundServerModeSettingIndex(value); }, "backgroundServerMode",
                       StrId::STR_CAT_SYSTEM, [] { return backgroundServerModeOptions(); })
                       .withConfiguratorExport("background_server_on_charge")
                       .withEnumOptionFeatureKeys({nullptr, nullptr, "background_server_always"}));
  }

  // Device name for mDNS/DHCP/AP SSID. Editable on-device via keyboard (STRING handler).
  // Input is sanitized to [a-z0-9-], max 24 chars, via validateAndClamp() on save.
  list.push_back(SettingInfo::Toggle(StrId::STR_DEVELOPER_MODE, &CrossPointSettings::developerMode, "developerMode",
                                     StrId::STR_CAT_ADVANCED)
                     .withConfiguratorExport());
  list.push_back(SettingInfo::String(StrId::STR_DEVICE_NAME, SETTINGS.deviceName, sizeof(SETTINGS.deviceName),
                                     "deviceName", StrId::STR_CAT_ADVANCED)
                     .withConfiguratorExport());

#if ENABLE_WIFI_CLOCK
  list.push_back(SettingInfo::Enum(StrId::STR_TIME_MODE, &CrossPointSettings::timeMode,
                                   {StrId::STR_TIME_UTC, StrId::STR_TIME_LOCAL, StrId::STR_TIME_MANUAL}, "timeMode",
                                   StrId::STR_CAT_TIME)
                     .withConfiguratorExport("wifi_clock"));
  list.push_back(SettingInfo::DynamicEnum(
                     StrId::STR_TIMEZONE_OFFSET, {}, [] { return SETTINGS.timeZoneOffset; },
                     [](uint8_t v) { SETTINGS.timeZoneOffset = std::min(v, uint8_t{26}); }, "timeZoneOffset",
                     StrId::STR_CAT_TIME, timezoneOffsetOptions)
                     .withConfiguratorExport("wifi_clock")
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

  return list;
}
