#pragma once

#include <FeatureFlags.h>
#include <HalStorage.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"
#include "SettingInfo.h"
#include "core/features/FeatureModules.h"
#include "util/TerminusCredentialStore.h"

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

inline std::vector<std::string> opdsFilenameFormatOptions() { return {"Author - Title", "Title - Author"}; }

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
      {StrId::STR_FOOTNOTES, S::FOOTNOTES},
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
  options.push_back({StrId::STR_FILE_TRANSFER, S::FILE_TRANSFER});
  return options;
}

inline std::vector<QuickActionOption> longPowerButtonOptions() {
  using S = CrossPointSettings;
  std::vector<QuickActionOption> options = {
      {StrId::STR_IGNORE, S::IGNORE},           {StrId::STR_SLEEP, S::SLEEP},
      {StrId::STR_PAGE_TURN, S::PAGE_TURN},     {StrId::STR_FORCE_REFRESH, S::FORCE_REFRESH},
      {StrId::STR_CHANGE_FONT, S::TOGGLE_FONT}, {StrId::STR_FOOTNOTES, S::FOOTNOTES},
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
  options.push_back({StrId::STR_FILE_TRANSFER, S::FILE_TRANSFER});
  return options;
}

#if ENABLE_DOUBLE_TAP_ACTION
inline std::vector<QuickActionOption> doubleTapPowerButtonOptions() {
  using S = CrossPointSettings;
  // DOUBLE_TAP_BACK first so "Back" appears as the legacy/fallback option.
  std::vector<QuickActionOption> options = {
      {StrId::STR_DOUBLE_TAP_BACK, S::DOUBLE_TAP_BACK}, {StrId::STR_IGNORE, S::IGNORE},
      {StrId::STR_FORCE_REFRESH, S::FORCE_REFRESH},     {StrId::STR_SLEEP, S::SLEEP},
      {StrId::STR_CHANGE_FONT, S::TOGGLE_FONT},         {StrId::STR_FOOTNOTES, S::FOOTNOTES},
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
  options.push_back({StrId::STR_FILE_TRANSFER, S::FILE_TRANSFER});
  return options;
}
#endif

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
  options.push_back({StrId::STR_FILE_TRANSFER, S::LONG_MENU_FILE_TRANSFER});
  options.push_back({StrId::STR_SELECT_TEXT, S::LONG_MENU_TEXT_SELECT, "text_selection"});
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
  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);

  // Build available built-in fonts in the same order as FontSelectionActivity,
  // guarded by the same compile-time flags. Position in this list is the index
  // the getter returns; stored value is SETTINGS.fontFamily (enum value).
  std::vector<uint8_t> builtinVals;
  std::vector<std::string> labels;
  std::vector<const char*> featureKeys;

  auto addBuiltin = [&](uint8_t val, StrId nameId, bool enabled, const char* featureKey) {
    if (enabled) {
      builtinVals.push_back(val);
      labels.push_back(I18N.get(nameId));
      featureKeys.push_back(featureKey);
    }
  };

#if ENABLE_BOOKERLY_FONTS
  addBuiltin(CrossPointSettings::NOTOSERIF, StrId::STR_NOTO_SERIF, true, "bookerly_fonts");
#endif
#if ENABLE_NOTOSANS_FONTS
  addBuiltin(CrossPointSettings::NOTOSANS, StrId::STR_NOTO_SANS, true, "notosans_fonts");
#endif
#if ENABLE_OPENDYSLEXIC_FONTS
  addBuiltin(CrossPointSettings::OPENDYSLEXIC, StrId::STR_OPEN_DYSLEXIC, true, "opendyslexic_fonts");
#endif
#if ENABLE_LEXENDDECA_FONTS
  addBuiltin(CrossPointSettings::LEXENDDECA, StrId::STR_LEXEND_DECA, true, "lexenddeca_fonts");
#endif
#if ENABLE_BITTER_FONTS
  addBuiltin(CrossPointSettings::BITTER, StrId::STR_BITTER, true, "bitter_fonts");
#endif
#if ENABLE_CHAREINK_FONTS
  addBuiltin(CrossPointSettings::CHAREINK, StrId::STR_CHARE_INK, true, "chareink_fonts");
#endif

  const int builtinCount = static_cast<int>(builtinVals.size());

  if (hasUserFonts) {
    labels.push_back(I18N.get(StrId::STR_EXTERNAL_FONT));
    featureKeys.push_back("user_fonts");
  }

  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    for (const auto& f : families) {
      sdFamilyNames.push_back(f.name);
      labels.push_back(f.name);
      featureKeys.push_back(nullptr);
    }
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumStringValues = labels;
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [builtinVals, sdFamilyNames, hasUserFonts, builtinCount]() -> uint8_t {
    if (hasUserFonts && SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
      return static_cast<uint8_t>(builtinCount);
    }
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      const int sdBase = builtinCount + (hasUserFonts ? 1 : 0);
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(sdBase + i);
        }
      }
    }
    for (int i = 0; i < builtinCount; i++) {
      if (builtinVals[i] == SETTINGS.fontFamily) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [builtinVals, sdFamilyNames, hasUserFonts, builtinCount](uint8_t v) {
    if (v < static_cast<uint8_t>(builtinCount)) {
      SETTINGS.fontFamily = builtinVals[v];
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else if (hasUserFonts && v == static_cast<uint8_t>(builtinCount)) {
      SETTINGS.fontFamily = CrossPointSettings::USER_SD;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      const int sdBase = builtinCount + (hasUserFonts ? 1 : 0);
      const int sdIdx = static_cast<int>(v) - sdBase;
      if (sdIdx >= 0 && sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
        if (SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
          SETTINGS.fontFamily = builtinVals.empty() ? CrossPointSettings::NOTOSERIF : builtinVals[0];
        }
      }
    }
  };

  s.withConfiguratorExport().withEnumOptionFeatureKeys(std::move(featureKeys));
  return s;
}

inline bool dirHasAnyImage(const char* path) {
  auto dir = Storage.open(path);
  if (!(dir && dir.isDirectory())) {
    if (dir) dir.close();
    return false;
  }

  char name[200];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }
    file.getName(name, sizeof(name));
    if (name[0] == '\0' || name[0] == '.') {
      file.close();
      continue;
    }
    file.close();

    std::string filename(name);
    if (filename.length() < 4) {
      continue;
    }
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const char* exts[] = {".bmp", ".png", ".jpg", ".jpeg"};
    bool found = false;
    for (const char* ext : exts) {
      size_t extLen = strlen(ext);
      if (lowerFilename.length() >= extLen &&
          lowerFilename.compare(lowerFilename.length() - extLen, extLen, ext) == 0) {
        found = true;
        break;
      }
    }
    if (found) {
      dir.close();
      return true;
    }
  }
  dir.close();
  return false;
}

inline void buildSleepModeOptions(std::vector<StrId>& ids, std::vector<uint8_t>& vals,
                                  std::vector<const char*>& optionFeatureKeys, bool hasSleepImages,
                                  bool hasPokedexImages) {
  using M = CrossPointSettings::SLEEP_SCREEN_MODE;

  ids.push_back(StrId::STR_DARK);
  vals.push_back(M::DARK);
  optionFeatureKeys.push_back(nullptr);

  ids.push_back(StrId::STR_LIGHT);
  vals.push_back(M::LIGHT);
  optionFeatureKeys.push_back(nullptr);

  ids.push_back(StrId::STR_FOLLOW_THEME);
  vals.push_back(M::FOLLOW_THEME);
  optionFeatureKeys.push_back(nullptr);

  const bool keepCustom = hasSleepImages || hasPokedexImages || (SETTINGS.sleepPinnedPath[0] != '\0');
  if (keepCustom) {
    ids.push_back(StrId::STR_CUSTOM);
    vals.push_back(M::CUSTOM);
    optionFeatureKeys.push_back("image_sleep");
  }

  ids.push_back(StrId::STR_BOOK_COVER);
  vals.push_back(M::COVER);
  optionFeatureKeys.push_back(nullptr);

  ids.push_back(StrId::STR_TRANSPARENT);
  vals.push_back(M::TRANSPARENT);
  optionFeatureKeys.push_back(nullptr);

#if ENABLE_ROMAN_CLOCK_SLEEP
  ids.push_back(StrId::STR_ROMAN_CLOCK);
  vals.push_back(M::ROMAN_CLOCK_SLEEP);
  optionFeatureKeys.push_back("roman_clock_sleep");
#endif
#if ENABLE_HAIKU_CLOCK
  ids.push_back(StrId::STR_HAIKU_CLOCK);
  vals.push_back(M::HAIKU_CLOCK_SLEEP);
  optionFeatureKeys.push_back("haiku_clock_sleep");
#endif
#if ENABLE_READING_STATS
  ids.push_back(StrId::STR_READING_STATS);
  vals.push_back(M::READING_STATS_SLEEP);
  optionFeatureKeys.push_back("reading_stats");
#endif
#if ENABLE_NOTES
  ids.push_back(StrId::STR_NOTES);
  vals.push_back(M::NOTES_SLEEP);
  optionFeatureKeys.push_back("notes");
#endif
#if ENABLE_TODO_PLANNER
  ids.push_back(StrId::STR_TODO_HOME_LABEL);
  vals.push_back(M::PLANNER_SLEEP);
  optionFeatureKeys.push_back("todo_planner");
#endif
}

inline bool sleepCustomOrCoverActive() {
  return CrossPointSettings::sleepModeActive(CrossPointSettings::CUSTOM) ||
         CrossPointSettings::sleepModeActive(CrossPointSettings::COVER);
}

#if ENABLE_HAIKU_CLOCK
inline bool sleepHaikuClockActive() {
  return CrossPointSettings::sleepModeActive(CrossPointSettings::HAIKU_CLOCK_SLEEP);
}
#endif

// Shared settings list for the device settings UI and web /api/settings.
// Each entry has a JSON key (SettingInfo::key) and StrId category; configuratorExport
// entries also carry a FeatureCatalog key (configuratorFeatureKey). ACTION entries
// and entries without a key are device-only.
// Function-pointer sink invoked once per setting, in display order. A plain
// function pointer (not std::function or a template) keeps this large generator
// a single inlined definition with zero per-call-site code bloat.
using SettingSink = void (*)(void* ctx, SettingInfo&& info);

// Streams every setting one at a time to `sink`. Performs NO SD/SPI access of
// its own: the caller pre-scans the sleep-image folders and supplies the
// already-built font family setting (both touch the SD card / font registry).
// This lets the concurrent web /api/settings handler stream the list without
// ever materializing a ~10 KB std::vector<SettingInfo>, and without holding the
// SPI bus across network writes (see streamSettingsListJson). getSettingsList()
// below is the buffered wrapper used on-device, where random access is needed.
inline void forEachSetting(SettingSink sink, void* ctx, bool hasSleepImages, bool hasPokedexImages,
                           SettingInfo&& fontFamilySetting) {
  // Emit one entry at a time. Taking `info` by value preserves the old
  // push_back(temporary) behavior: each entry is constructed on the stack, then
  // moved into the sink — never N entries live simultaneously. (A braced
  // std::initializer_list would back all N at once, spiking the ~8 KB loopTask
  // stack by ~6 KB; SettingInfo is ~200 bytes — five std::function members.)
  auto emit = [&](SettingInfo info) { sink(ctx, std::move(info)); };

  // --- Display ---
  // Sleep screen uses DynamicEnum with explicit value mapping so display order
  // stays independent of the persisted enum values.
  emit(SettingInfo::Enum(StrId::STR_SLEEP_STYLE, &CrossPointSettings::sleepScreenSplit,
                         {StrId::STR_SLEEP_UNIFIED, StrId::STR_SLEEP_SMART}, "sleepScreenSplit", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());

  emit([&] {
    std::vector<StrId> ids;
    std::vector<uint8_t> vals;
    std::vector<const char*> optionFeatureKeys;
    buildSleepModeOptions(ids, vals, optionFeatureKeys, hasSleepImages, hasPokedexImages);
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
        .withVisibleWhen("sleepScreenSplit", CrossPointSettings::SLEEP_SPLIT_UNIFIED)
        .withConfiguratorExport()
        .withEnumPersistedValues(vals)
        .withEnumOptionFeatureKeys(std::move(optionFeatureKeys));
  }());

  emit([&] {
    std::vector<StrId> ids;
    std::vector<uint8_t> vals;
    std::vector<const char*> optionFeatureKeys;
    buildSleepModeOptions(ids, vals, optionFeatureKeys, hasSleepImages, hasPokedexImages);
    return SettingInfo::DynamicEnum(
               StrId::STR_SLEEP_READER_SCREEN, ids,
               [vals] {
                 const uint8_t cur = SETTINGS.sleepScreenReader;
                 for (size_t i = 0; i < vals.size(); i++) {
                   if (vals[i] == cur) return static_cast<uint8_t>(i);
                 }
                 return uint8_t{0};
               },
               [vals](uint8_t idx) {
                 if (idx < vals.size()) SETTINGS.sleepScreenReader = vals[idx];
               },
               "sleepScreenReader", StrId::STR_CAT_DISPLAY)
        .withVisibleWhen("sleepScreenSplit", CrossPointSettings::SLEEP_SPLIT_SMART)
        .withConfiguratorExport()
        .withEnumPersistedValues(vals)
        .withEnumOptionFeatureKeys(std::move(optionFeatureKeys));
  }());

  emit([&] {
    std::vector<StrId> ids;
    std::vector<uint8_t> vals;
    std::vector<const char*> optionFeatureKeys;
    buildSleepModeOptions(ids, vals, optionFeatureKeys, hasSleepImages, hasPokedexImages);
    return SettingInfo::DynamicEnum(
               StrId::STR_SLEEP_GENERAL_SCREEN, ids,
               [vals] {
                 const uint8_t cur = SETTINGS.sleepScreenHome;
                 for (size_t i = 0; i < vals.size(); i++) {
                   if (vals[i] == cur) return static_cast<uint8_t>(i);
                 }
                 return uint8_t{0};
               },
               [vals](uint8_t idx) {
                 if (idx < vals.size()) SETTINGS.sleepScreenHome = vals[idx];
               },
               "sleepScreenHome", StrId::STR_CAT_DISPLAY)
        .withVisibleWhen("sleepScreenSplit", CrossPointSettings::SLEEP_SPLIT_SMART)
        .withConfiguratorExport()
        .withEnumPersistedValues(vals)
        .withEnumOptionFeatureKeys(std::move(optionFeatureKeys));
  }());
  {
    std::vector<StrId> sleepSourceLabels = {StrId::STR_SLEEP};
    std::vector<const char*> sleepSourceFeatureKeys = {nullptr};
    if (hasPokedexImages) {
      sleepSourceLabels.push_back(StrId::STR_POKEDEX);
      sleepSourceLabels.push_back(StrId::STR_ALL);
      sleepSourceFeatureKeys.push_back("pokemon_party");
      sleepSourceFeatureKeys.push_back("pokemon_party");
    }
    emit(SettingInfo::Enum(StrId::STR_SLEEP_SOURCE, &CrossPointSettings::sleepScreenSource, sleepSourceLabels,
                           "sleepScreenSource", StrId::STR_CAT_DISPLAY)
             .withConfiguratorExport()
             .withEnumOptionFeatureKeys(sleepSourceFeatureKeys)
             .withVisiblePredicate(sleepCustomOrCoverActive));
  }
  emit(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                         {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport()
           .withVisiblePredicate(sleepCustomOrCoverActive));
  emit(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                         {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                         "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_SLEEP_CYCLE_MODE, &CrossPointSettings::sleepCycleMode,
                         {StrId::STR_RANDOM, StrId::STR_SEQUENTIAL}, "sleepCycleMode", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport()
           .withVisiblePredicate(sleepCustomOrCoverActive));
  emit(SettingInfo::Toggle(StrId::STR_CLEAN_SLEEP_REFRESH, &CrossPointSettings::cleanSleepRefresh, "cleanSleepRefresh",
                           StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport("clean_sleep_refresh"));
#if ENABLE_HAIKU_CLOCK
  emit(SettingInfo::Toggle(StrId::STR_HAIKU_CLOCK_LANDSCAPE, &CrossPointSettings::haikuClockLandscape,
                           "haikuClockLandscape", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport()
           .withVisiblePredicate(sleepHaikuClockActive));
#endif
  emit(SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                           "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                           "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                         {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                         StrId::STR_CUSTOMISE_STATUS_BAR)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                         {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                         "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR)
           .withConfiguratorExport()
           .withVisibleWhenNot("statusBarProgressBar", CrossPointSettings::HIDE_PROGRESS));
  emit(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                         {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                         StrId::STR_CUSTOMISE_STATUS_BAR)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                           StrId::STR_CUSTOMISE_STATUS_BAR)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                         {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                         StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(
           StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
           {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
           "refreshFrequency", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());
  if (core::FeatureModules::hasCapability(core::Capability::CalibreSync)) {
    emit(SettingInfo::DynamicEnum(
             StrId::STR_FILENAME, {}, [] { return SETTINGS.opdsFilenameFormat; },
             [](uint8_t value) {
               SETTINGS.opdsFilenameFormat = value < CrossPointSettings::OPDS_FILENAME_FORMAT_COUNT
                                                 ? value
                                                 : CrossPointSettings::OPDS_FILENAME_AUTHOR_TITLE;
             },
             "opdsFilenameFormat", StrId::STR_CAT_SYSTEM, opdsFilenameFormatOptions)
             .withConfiguratorExport("calibre_sync"));
  }
  // Build options with explicit enum-value mapping so position != value assumptions
  // don't break when individual themes are optionally included or excluded.
  emit([] {
    std::vector<StrId> ids = {StrId::STR_THEME_CLASSIC};
    std::vector<uint8_t> vals = {CrossPointSettings::UI_THEME::CLASSIC};
    std::vector<const char*> optionFeatureKeys = {nullptr};
    if (core::FeatureModules::hasCapability(core::Capability::LyraTheme)) {
      ids.insert(ids.end(), {StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED, StrId::STR_THEME_FORK_DRIFT,
                             StrId::STR_THEME_LYRA_CAROUSEL, StrId::STR_THEME_TERMINAL});
      vals.insert(vals.end(), {CrossPointSettings::UI_THEME::LYRA, CrossPointSettings::UI_THEME::LYRA_EXTENDED,
                               CrossPointSettings::UI_THEME::FORK_DRIFT, CrossPointSettings::UI_THEME::LYRA_CAROUSEL,
                               CrossPointSettings::UI_THEME::TERMINAL});
      optionFeatureKeys.insert(optionFeatureKeys.end(),
                               {"lyra_theme", "lyra_theme", "lyra_theme", "lyra_theme", "lyra_theme"});
#if ENABLE_FLOW_THEME
      ids.push_back(StrId::STR_THEME_FLOW);
      vals.push_back(CrossPointSettings::UI_THEME::FLOW);
      optionFeatureKeys.push_back("flow_theme");
#endif
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
  emit(SettingInfo::Enum(StrId::STR_RECENT_BOOKS_VIEW, &CrossPointSettings::recentBooksView,
                         {StrId::STR_LIST_VIEW, StrId::STR_GRID_VIEW}, "recentBooksView", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                           StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_SHOW_BUTTON_HINTS, &CrossPointSettings::showButtonHints, "showButtonHints",
                           StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());

  // --- Reader ---
  emit(std::move(fontFamilySetting));
  emit(SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                         {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}, "fontSize",
                         StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                         {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing", StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, {5, 40, 5}, "screenMargin",
                          StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                         {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                          StrId::STR_BOOK_S_STYLE},
                         "paragraphAlignment", StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                           StrId::STR_CAT_READER)
           .withConfiguratorExport());
  if (core::FeatureModules::hasCapability(core::Capability::FocusReading)) {
    emit(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled, "focusReadingEnabled",
                             StrId::STR_CAT_READER)
             .withConfiguratorExport("focus_reading"));
  }
  if (core::FeatureModules::hasCapability(core::Capability::GuideDots)) {
    emit(SettingInfo::Toggle(StrId::STR_GUIDE_READING, &CrossPointSettings::guideReadingEnabled, "guideReadingEnabled",
                             StrId::STR_CAT_READER)
             .withConfiguratorExport("guide_dots"));
  }
  emit(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                           StrId::STR_CAT_READER)
           .withConfiguratorExport("hyphenation"));
  emit(SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                         {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                         "orientation", StrId::STR_CAT_READER)
           .withConfiguratorExport());
#if ENABLE_GLOBAL_LANDSCAPE
  emit(SettingInfo::Enum(StrId::STR_UI_ORIENTATION, &CrossPointSettings::uiOrientation,
                         {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW},
                         "uiOrientation", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport());
#endif
  emit(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                           "extraParagraphSpacing", StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_FORCE_PARAGRAPH_INDENTS, &CrossPointSettings::forceParagraphIndents,
                           "forceParagraphIndents", StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                           StrId::STR_CAT_READER)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                         {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                         "imageRendering", StrId::STR_CAT_READER)
           .withConfiguratorExport("book_images"));

  // --- Controls ---
  emit(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                         {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}, "sideButtonLayout",
                         StrId::STR_CAT_CONTROLS)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_ORIENTATION_AWARE, &CrossPointSettings::sideButtonOrientationAware,
                         {StrId::STR_NO, StrId::STR_YES}, "sideButtonOrientationAware", StrId::STR_CAT_CONTROLS)
           .withConfiguratorExport()
           .withVisibleWhenNot("orientation", CrossPointSettings::PORTRAIT));
  emit(SettingInfo::Enum(StrId::STR_SIDE_BTN_LONG_PRESS, &CrossPointSettings::sideButtonLongPress,
                         {StrId::STR_CHAPTER_SKIP_OPT, StrId::STR_CHANGE_FONT_SIZE, StrId::STR_OFF},
                         "sideButtonLongPress", StrId::STR_CAT_CONTROLS)
           .withConfiguratorExport());
  emit(SettingInfo::Enum(StrId::STR_ORIENTATION_AWARE, &CrossPointSettings::frontButtonOrientationAware,
                         {StrId::STR_NO, StrId::STR_NAV_BUTTONS, StrId::STR_ALL_BUTTONS}, "frontButtonOrientationAware",
                         StrId::STR_CAT_CONTROLS)
           .withConfiguratorExport()
           .withVisibleWhenNot("orientation", CrossPointSettings::PORTRAIT));
  emit(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                         {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                          StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                         "longPressButtonBehavior", StrId::STR_CAT_CONTROLS)
           .withConfiguratorExport());
  emit([] {
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
  emit([] {
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
#if ENABLE_DOUBLE_TAP_ACTION
  emit([] {
    const auto options = doubleTapPowerButtonOptions();
    SettingInfo setting = SettingInfo::DynamicEnum(
        StrId::STR_DOUBLE_TAP_PWR_BTN, {},
        [options] { return quickActionOptionIndex(options, SETTINGS.doubleTapPwrBtn); },
        [options](uint8_t index) {
          SETTINGS.doubleTapPwrBtn = quickActionValueForIndex(options, index, CrossPointSettings::FORCE_REFRESH);
        },
        "doubleTapPwrBtn", StrId::STR_CAT_CONTROLS, [options] { return quickActionOptionLabels(options); });
    setting.withConfiguratorExport()
        .withEnumPersistedValues(quickActionPersistedValues(options))
        .withEnumOptionFeatureKeys(quickActionFeatureKeys(options));
    return setting;
  }());
#endif
  emit([] {
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
  emit(SettingInfo::Value(
           StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
           {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
           "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM)
           .withConfiguratorExport());
  emit(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                           StrId::STR_CAT_SYSTEM)
           .withConfiguratorExport());
#if ENABLE_TODO_PLANNER
  if (core::FeatureModules::hasCapability(core::Capability::TodoPlanner)) {
    emit(SettingInfo::Toggle(StrId::STR_TODO_OPEN_DIRECT_TO_TODAY, &CrossPointSettings::todoOpenDirectToToday,
                             "todoOpenDirectToToday", StrId::STR_CAT_SYSTEM)
             .withConfiguratorExport("todo_planner"));
  }
#endif
#if ENABLE_READING_STATS
  emit(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                           "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM)
           .withConfiguratorExport("reading_stats"));
#endif

  if (core::FeatureModules::hasCapability(core::Capability::TerminusSleep)) {
    if (TERMINUS_STORE.hasCredentials()) {
      emit(SettingInfo::Toggle(StrId::STR_TERMINUS_SLEEP_ENABLED, &CrossPointSettings::terminusSleepEnabled,
                               "terminusSleepEnabled", StrId::STR_CAT_DISPLAY)
               .withConfiguratorExport("terminus_sleep"));
    }
    // Terminus credentials are managed via /.crosspoint/terminus.json or /plugins/terminus web UI.
    // A settings action entry allows navigating to the setup page from the on-device settings menu.
    emit(SettingInfo::Action(StrId::STR_TERMINUS_SETUP, SettingAction::TerminusSetup));
  }
#if ENABLE_TIMED_SLEEP_REFRESH
  emit(SettingInfo::Enum(StrId::STR_TIMED_REFRESH_INTERVAL, &CrossPointSettings::timedSleepRefreshInterval,
                         {StrId::STR_OFF, StrId::STR_TIMED_REFRESH_1H, StrId::STR_TIMED_REFRESH_2H,
                          StrId::STR_TIMED_REFRESH_4H, StrId::STR_TIMED_REFRESH_8H, StrId::STR_TIMED_REFRESH_24H},
                         "timedSleepRefreshInterval", StrId::STR_CAT_DISPLAY)
           .withConfiguratorExport("timed_sleep_refresh"));
#endif
  // Boot-partition switch into a co-installed TRMNL firmware; only meaningful
  // once Terminus credentials prove the user is on the TRMNL ecosystem.
  if (core::FeatureModules::hasCapability(core::Capability::TrmnlSwitch) && TERMINUS_STORE.hasCredentials()) {
    emit(SettingInfo::Action(StrId::STR_SWITCH_TO_TRMNL, SettingAction::SwitchToTrmnl));
  }
#if ENABLE_ANKI_SUPPORT
  emit(SettingInfo::String(StrId::STR_ANKI_CONNECT_URL, SETTINGS.ankiConnectUrl, sizeof(SETTINGS.ankiConnectUrl),
                           "ankiConnectUrl", StrId::STR_CAT_SYSTEM)
           .withConfiguratorExport("anki_support"));
  emit(SettingInfo::String(StrId::STR_ANKI_CONNECT_DECK, SETTINGS.ankiConnectDeck, sizeof(SETTINGS.ankiConnectDeck),
                           "ankiConnectDeck", StrId::STR_CAT_SYSTEM)
           .withConfiguratorExport("anki_support"));
#endif

  if (core::FeatureModules::hasCapability(core::Capability::DarkMode)) {
    emit(SettingInfo::Enum(StrId::STR_DARK_MODE, &CrossPointSettings::darkModeScope,
                           {StrId::STR_OFF, StrId::STR_DARK_MODE_READER_ONLY, StrId::STR_DARK_MODE_EVERYWHERE},
                           "darkModeScope", StrId::STR_CAT_DISPLAY)
             .withConfiguratorExport("dark_mode"));
  }

  if (core::FeatureModules::hasCapability(core::Capability::GlobalStatusBar)) {
    emit(SettingInfo::Enum(StrId::STR_STATUS_BAR_POSITION, &CrossPointSettings::globalStatusBarPosition,
                           {StrId::STR_STATUS_BAR_TOP, StrId::STR_STATUS_BAR_BOTTOM, StrId::STR_OFF},
                           "globalStatusBarPosition", StrId::STR_CAT_DISPLAY)
             .withConfiguratorExport("global_status_bar"));
  }

  if (supportsBackgroundServerModeSetting()) {
    emit(SettingInfo::DynamicEnum(
             StrId::STR_BACKGROUND_SERVER, {}, [] { return getBackgroundServerModeSettingIndex(); },
             [](uint8_t value) { setBackgroundServerModeSettingIndex(value); }, "backgroundServerMode",
             StrId::STR_CAT_SYSTEM, [] { return backgroundServerModeOptions(); })
             .withConfiguratorExport("background_server_on_charge")
             .withEnumOptionFeatureKeys({nullptr, nullptr, "background_server_always"}));
#if ENABLE_WIFI_CLOCK
    emit(SettingInfo::Toggle(StrId::STR_CLOCK_SYNC, &CrossPointSettings::autoSyncDayOnBackgroundPing,
                             "autoSyncDayOnBackgroundPing", StrId::STR_CAT_SYSTEM)
             .withConfiguratorExport("wifi_clock"));
#endif
  }

  // Device name for mDNS/DHCP/AP SSID. Editable on-device via keyboard (STRING handler).
  // Input is sanitized to [a-z0-9-], max 24 chars, via validateAndClamp() on save.
  emit(SettingInfo::Toggle(StrId::STR_DEVELOPER_MODE, &CrossPointSettings::developerMode, "developerMode",
                           StrId::STR_CAT_ADVANCED)
           .withConfiguratorExport());
  emit(SettingInfo::String(StrId::STR_DEVICE_NAME, SETTINGS.deviceName, sizeof(SETTINGS.deviceName), "deviceName",
                           StrId::STR_CAT_ADVANCED)
           .withConfiguratorExport()
           .withVisibleWhen("developerMode", 1));

#if ENABLE_WIFI_CLOCK
  emit(SettingInfo::Enum(StrId::STR_TIME_MODE, &CrossPointSettings::timeMode,
                         {StrId::STR_TIME_UTC, StrId::STR_TIME_LOCAL, StrId::STR_TIME_MANUAL}, "timeMode",
                         StrId::STR_CAT_TIME)
           .withConfiguratorExport("wifi_clock"));
  emit(SettingInfo::DynamicEnum(
           StrId::STR_TIMEZONE_OFFSET, {}, [] { return SETTINGS.timeZoneOffset; },
           [](uint8_t v) { SETTINGS.timeZoneOffset = std::min(v, uint8_t{26}); }, "timeZoneOffset", StrId::STR_CAT_TIME,
           timezoneOffsetOptions)
           .withConfiguratorExport("wifi_clock"));
#endif

  if (core::FeatureModules::hasCapability(core::Capability::KoreaderSync)) {
    // --- KOReader Sync (web-only, persisted via FeatureModules) ---
    emit(SettingInfo::DynamicString(
        StrId::STR_KOREADER_USERNAME, [] { return core::FeatureModules::getKoreaderUsername(); },
        [](const std::string& value) { core::FeatureModules::setKoreaderUsername(value, false); }, "koUsername",
        StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicString(
        StrId::STR_KOREADER_PASSWORD, [] { return core::FeatureModules::getKoreaderPassword(); },
        [](const std::string& value) { core::FeatureModules::setKoreaderPassword(value, false); }, "koPassword",
        StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicString(
        StrId::STR_SYNC_SERVER_URL, [] { return core::FeatureModules::getKoreaderServerUrl(); },
        [](const std::string& value) { core::FeatureModules::setKoreaderServerUrl(value, false); }, "koServerUrl",
        StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicEnum(
        StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
        [] { return core::FeatureModules::getKoreaderMatchMethod(); },
        [](uint8_t value) { core::FeatureModules::setKoreaderMatchMethod(value, false); }, "koMatchMethod",
        StrId::STR_KOREADER_SYNC));
  }

  if (core::FeatureModules::hasCapability(core::Capability::UserFonts)) {
    emit(SettingInfo::DynamicEnum(
        StrId::STR_EXTERNAL_FONT, {}, [] { return core::FeatureModules::getSelectedUserFontFamilyIndex(); },
        [](uint8_t value) { core::FeatureModules::setSelectedUserFontFamilyIndex(value); }, "userFontPath",
        StrId::STR_CAT_READER, [] { return core::FeatureModules::getUserFontFamilies(); }));
  }

  emit(SettingInfo::Action(StrId::STR_BACKUP_SETTINGS, SettingAction::BackupSettings));
  emit(SettingInfo::Action(StrId::STR_RESTORE_SETTINGS, SettingAction::RestoreSettings));
}

// Buffered settings list for on-device callers (SettingsActivity and the reader
// option screens) that need random access / category filtering. Materializes
// the full vector, which is acceptable on the main loop task — it gates on free
// heap before calling. The concurrent web path uses forEachSetting +
// streamSettingsListJson instead, to avoid this ~10 KB allocation under low heap.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr) {
  // Sleep-image folders are scanned once per list build; both the Custom option
  // and the Pokédex source options are hidden when there is nothing to show.
  const bool hasSleepImages = dirHasAnyImage("/sleep");
  const bool hasPokedexImages = dirHasAnyImage("/sleep/pokedex");

  std::vector<SettingInfo> list;
  list.reserve(48);  // Upper-bound estimate; avoids repeated 2× heap reallocation.
  forEachSetting(
      [](void* ctx, SettingInfo&& info) { static_cast<std::vector<SettingInfo>*>(ctx)->push_back(std::move(info)); },
      &list, hasSleepImages, hasPokedexImages, buildFontFamilySetting(registry));
  return list;
}
