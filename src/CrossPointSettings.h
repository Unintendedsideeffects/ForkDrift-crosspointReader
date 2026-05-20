#pragma once
#include <FeatureFlags.h>
#include <HalStorage.h>

#include <cstdint>
#include <iosfwd>

class CrossPointSettings {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  // Static instance
  static CrossPointSettings instance;

 public:
  // Delete copy constructor and assignment
  CrossPointSettings(const CrossPointSettings&) = delete;
  CrossPointSettings& operator=(const CrossPointSettings&) = delete;

  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    TRANSPARENT = 3,
    FOLLOW_THEME = 4,
    // NOTE: 5 and 6 are intercepted as legacy migrations in validateAndClamp(). Do not use.
    SMART = 7,
    ROMAN_CLOCK_SLEEP = 8,
    SLEEP_SCREEN_MODE_COUNT = 9,
    // Legacy raw values — never assigned via UI; handled in validateAndClamp().
    COVER = 9,          // was 3
    BLANK = 10,         // was 4
    COVER_CUSTOM = 11,  // was 5
    READING_STATS_SLEEP = 12,
    // Old TRANSPARENT was 6 — migrated to new TRANSPARENT=3 in validateAndClamp().
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };
  enum SLEEP_SCREEN_SOURCE {
    SLEEP_SOURCE_SLEEP = 0,
    SLEEP_SOURCE_POKEDEX = 1,
    SLEEP_SOURCE_ALL = 2,
    SLEEP_SCREEN_SOURCE_COUNT
  };
  enum SLEEP_CYCLE_MODE { SLEEP_CYCLE_RANDOM = 0, SLEEP_CYCLE_SEQUENTIAL = 1, SLEEP_CYCLE_MODE_COUNT };

  // Status bar display type enum
  enum STATUS_BAR_MODE {
    NONE = 0,
    NO_PROGRESS = 1,
    FULL = 2,
    BOOK_PROGRESS_BAR = 3,
    ONLY_BOOK_PROGRESS_BAR = 4,
    CHAPTER_PROGRESS_BAR = 5,
    STATUS_BAR_MODE_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR {
    BOOK_PROGRESS = 0,
    CHAPTER_PROGRESS = 1,
    HIDE_PROGRESS = 2,
    STATUS_BAR_PROGRESS_BAR_COUNT
  };
  enum STATUS_BAR_PROGRESS_BAR_THICKNESS {
    PROGRESS_BAR_THIN = 0,
    PROGRESS_BAR_NORMAL = 1,
    PROGRESS_BAR_THICK = 2,
    STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT
  };
  enum STATUS_BAR_TITLE { BOOK_TITLE = 0, CHAPTER_TITLE = 1, HIDE_TITLE = 2, STATUS_BAR_TITLE_COUNT };

  enum ORIENTATION {
    PORTRAIT = 0,       // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,   // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,       // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3,  // 800x480 logical coordinates, native panel orientation
    ORIENTATION_COUNT
  };

  // Front button layout options (legacy)
  // Default: Back, Confirm, Left, Right
  // Swapped: Left, Right, Back, Confirm
  enum FRONT_BUTTON_LAYOUT {
    BACK_CONFIRM_LEFT_RIGHT = 0,
    LEFT_RIGHT_BACK_CONFIRM = 1,
    LEFT_BACK_CONFIRM_RIGHT = 2,
    BACK_CONFIRM_RIGHT_LEFT = 3,
    LEFT_LEFT_RIGHT_RIGHT = 4,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Previous, Next
  // Swapped: Next, Previous
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTON_LAYOUT_COUNT };

  enum FRONT_BUTTON_ORIENTATION_AWARE {
    FRONT_ORIENTATION_AWARE_OFF = 0,
    FRONT_ORIENTATION_AWARE_NAV_BUTTONS = 1,
    FRONT_ORIENTATION_AWARE_ALL_BUTTONS = 2,
    FRONT_ORIENTATION_AWARE_COUNT
  };

  // Side button long-press action options
  enum SIDE_LONG_PRESS {
    SIDE_LONG_CHAPTER_SKIP = 0,
    SIDE_LONG_FONT_SIZE = 1,
    SIDE_LONG_OFF = 2,
    SIDE_LONG_PRESS_COUNT
  };

  // Font family options (built-in fonts plus fork/user-managed external fonts).
  // sdFontFamilyName selects an SD-card family when non-empty.
  enum FONT_FAMILY {
    BOOKERLY = 0,
    NOTOSERIF = 0,
    NOTOSANS = 1,
    OPENDYSLEXIC = 2,
    LEXENDDECA = 3,
    BITTER = 4,
    CHAREINK = 5,
    USER_SD = 6,
    FONT_FAMILY_COUNT
  };
  static constexpr uint8_t BUILTIN_FONT_COUNT = USER_SD;
  // Font size options
  enum FONT_SIZE { SMALL = 0, MEDIUM = 1, LARGE = 2, EXTRA_LARGE = 3, FONT_SIZE_COUNT };
  enum LINE_COMPRESSION { TIGHT = 0, NORMAL = 1, WIDE = 2, LINE_COMPRESSION_COUNT };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };
  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = 30;
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);
  static uint8_t normalizeSleepScreenMode(uint8_t rawValue);

  // E-ink refresh frequency (pages between full refreshes)
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_FREQUENCY_COUNT
  };

  // Short power button press actions
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    SELECT = 3,
    FORCE_REFRESH = 4,
    TOGGLE_FONT = 5,
    TOGGLE_GUIDE_DOTS = 6,
    TOGGLE_BIONIC_READING = 7,
    TOGGLE_BOOKMARK = 8,
    SYNC_PROGRESS = 9,
    MARK_FINISHED = 10,
    READING_STATS = 11,
    SCREENSHOT = 12,
    CYCLE_PAGE_TURN = 13,
    FILE_TRANSFER = 14,
    SHORT_PWRBTN_COUNT
  };

  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };
  enum UI_THEME {
    CLASSIC = 0,
    LYRA = 1,
    LYRA_EXTENDED = 2,
    FORK_DRIFT = 3,
    POKEMON_PARTY = 4,
    MINIMAL = 5,
    LYRA_CAROUSEL = 6
  };
  enum RECENT_BOOKS_VIEW { RECENT_BOOKS_LIST = 0, RECENT_BOOKS_GRID = 1, RECENT_BOOKS_VIEW_COUNT };

  // Page turn button long press behavior
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    OFF = 0,
    CHAPTER_SKIP = 1,
    ORIENTATION_CHANGE = 2,
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };
  // Time mode options
  enum TIME_MODE { TIME_MODE_UTC = 0, TIME_MODE_LOCAL = 1, TIME_MODE_MANUAL = 2 };

  // Release channel options
  enum RELEASE_CHANNEL {
    RELEASE_STABLE = 0,
    RELEASE_NIGHTLY = 1,
    RELEASE_LATEST_SUCCESSFUL = 2,
    RELEASE_LATEST_SUCCESSFUL_FACTORY_RESET = 3,
    RELEASE_CHANNEL_COUNT
  };
  enum BACKGROUND_SERVER_MODE {
    BACKGROUND_SERVER_NEVER = 0,
    BACKGROUND_SERVER_ON_CHARGE = 1,
    BACKGROUND_SERVER_ALWAYS = 2,
    BACKGROUND_SERVER_MODE_COUNT
  };

  enum GLOBAL_STATUS_BAR_MODE {
    GLOBAL_STATUS_BAR_OFF = 0,
    GLOBAL_STATUS_BAR_ON = 1,
    GLOBAL_STATUS_BAR_NO_SLEEP = 2,
    GLOBAL_STATUS_BAR_MODE_COUNT
  };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };

  // Long-press Confirm (menu button) quick action in reader
  enum LONG_PRESS_MENU_ACTION {
    LONG_MENU_OFF = 0,
    LONG_MENU_SLEEP = 1,
    LONG_MENU_CHANGE_FONT = 2,
    LONG_MENU_TOGGLE_GUIDE_DOTS = 3,
    LONG_MENU_TOGGLE_BIONIC = 4,
    LONG_MENU_TOGGLE_BOOKMARK = 5,
    LONG_MENU_REFRESH_SCREEN = 6,
    LONG_MENU_SYNC_PROGRESS = 7,
    LONG_MENU_MARK_FINISHED = 8,
    LONG_MENU_READING_STATS = 9,
    LONG_MENU_SCREENSHOT = 10,
    LONG_MENU_CYCLE_PAGE_TURN = 11,
    LONG_MENU_FILE_TRANSFER = 12,
    LONG_PRESS_MENU_ACTION_COUNT
  };

  // Global status bar overlay position
  enum GLOBAL_STATUS_BAR_POSITION { STATUS_BAR_TOP = 0, STATUS_BAR_BOTTOM = 1, GLOBAL_STATUS_BAR_POSITION_COUNT };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Sleep screen custom-image source
  uint8_t sleepScreenSource = SLEEP_SOURCE_SLEEP;
  // Sleep image cycle mode (random or sequential)
  uint8_t sleepCycleMode = SLEEP_CYCLE_RANDOM;
  // Pinned sleep cover path — if non-empty and sleepScreen==CUSTOM, always use this image.
  char sleepPinnedPath[256] = "";
  // Status bar settings (statusBar retained for migration only)
  uint8_t statusBar = FULL;
  uint8_t statusBarChapterPageCount = 1;
  uint8_t statusBarBookProgressPercentage = 1;
  uint8_t statusBarProgressBar = HIDE_PROGRESS;
  uint8_t statusBarProgressBarThickness = PROGRESS_BAR_NORMAL;
  uint8_t statusBarTitle = CHAPTER_TITLE;
  uint8_t statusBarBattery = 1;
  // Clock display in status bar (X3 only, requires DS3231 RTC)
  uint8_t statusBarClock = 0;
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t.
  // Value 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Set once an NTP sync succeeds.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  uint8_t forceParagraphIndents = 0;
  uint8_t textAntiAliasing = 1;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Button layouts
  uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  uint8_t sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonOrientationAware = FRONT_ORIENTATION_AWARE_OFF;
  uint8_t sideButtonOrientationAware = 0;
  // Action performed when side buttons are long-pressed in reader
  uint8_t sideButtonLongPress = SIDE_LONG_CHAPTER_SKIP;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = BOOKERLY;
  uint8_t fontSize = MEDIUM;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes)
  uint8_t sleepTimeout = SLEEP_10_MIN;  // legacy enum retained for binary/JSON migration
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  uint8_t hyphenationEnabled = 0;

  // Reader screen margin settings
  uint8_t screenMargin = 5;
  // OPDS browser settings
  char opdsServerUrl[128] = "";
  char opdsUsername[64] = "";
  char opdsPassword[64] = "";
  // Hide battery percentage
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  // Long-press page turn button behavior
  uint8_t longPressButtonBehavior = CHAPTER_SKIP;
  // UI theme
  uint8_t uiTheme = FORK_DRIFT;
  uint8_t recentBooksView = RECENT_BOOKS_LIST;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Deprecated JSON migration fallback for old boolean long-press chapter skip.
  uint8_t longPressChapterSkip = 1;
  // Use book's embedded CSS styles for EPUB rendering
  uint8_t embeddedStyle = 1;
  // Persisted background server flag for charge-only and always-on modes.
  uint8_t backgroundServerOnCharge = ENABLE_BACKGROUND_SERVER_ON_CHARGE || ENABLE_BACKGROUND_SERVER_ALWAYS;
  // Deprecated: persisted for backward compat, not consumed at runtime
  uint8_t todoFallbackCover = 0;
  // Time settings
  uint8_t timeMode = TIME_MODE_UTC;
  // Timezone offset index: 0 = UTC-12, 12 = UTC+0, 26 = UTC+14
  uint8_t timeZoneOffset = 12;
  // Last successful NTP sync (epoch seconds, UTC)
  uint32_t lastTimeSyncEpoch = 0;
  // OTA release channel selection
  uint8_t releaseChannel = RELEASE_STABLE;
  uint8_t darkMode = 0;
  uint8_t usbMscPromptOnConnect = 0;
  char userFontPath[128] = "";
  char selectedOtaBundle[32] = "";
  char installedOtaBundle[32] = "";
  char installedOtaFeatureFlags[192] = "";
  // Network identity — used for mDNS hostname, DHCP hostname, and AP SSID.
  // Only [a-z0-9-] chars; max 24 chars. Empty = fall back to last-4-MAC.
  char deviceName[32] = "";
  // Persisted background server flag for always-on mode while the device is awake.
  uint8_t wifiAutoConnect = ENABLE_BACKGROUND_SERVER_ALWAYS;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // Guide Dots - inserts a middle dot between words as a visual reading guide
  uint8_t guideReadingEnabled = 0;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  uint8_t moveFinishedToReadFolder = 0;
  // Mirror firmware logs to /crosspoint-debug.log on the SD card.
  uint8_t developerMode = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Long-press power button action in reader (uses SHORT_PWRBTN enum; default = sleep)
  uint8_t longPwrBtn = SLEEP;
  // Long-press Confirm (menu button) quick action in reader (0 = off)
  uint8_t longPressMenuAction = LONG_MENU_OFF;
  // Global status bar overlay (battery + WiFi, always visible across all screens)
  uint8_t globalStatusBar = GLOBAL_STATUS_BAR_OFF;
  uint8_t globalStatusBarPosition = STATUS_BAR_TOP;  // 0 = top, 1 = bottom
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings& getInstance() { return instance; }

  static constexpr bool supportsBackgroundServerOnChargeMode() {
    return ENABLE_BACKGROUND_SERVER_ON_CHARGE != 0 || ENABLE_BACKGROUND_SERVER_ALWAYS != 0;
  }

  static constexpr bool supportsBackgroundServerAlwaysMode() { return ENABLE_BACKGROUND_SERVER_ALWAYS != 0; }

  bool isGlobalStatusBarEnabled() const { return globalStatusBar != GLOBAL_STATUS_BAR_OFF; }

  bool globalStatusBarPreventsAutoSleep() const { return globalStatusBar == GLOBAL_STATUS_BAR_NO_SLEEP; }

  uint8_t getBackgroundServerMode() const {
    if (supportsBackgroundServerAlwaysMode() && wifiAutoConnect) {
      return BACKGROUND_SERVER_ALWAYS;
    }
    if (supportsBackgroundServerOnChargeMode() && backgroundServerOnCharge) {
      return BACKGROUND_SERVER_ON_CHARGE;
    }
    return BACKGROUND_SERVER_NEVER;
  }

  void setBackgroundServerMode(const uint8_t mode) {
    switch (mode) {
      case BACKGROUND_SERVER_ALWAYS:
        if (supportsBackgroundServerAlwaysMode()) {
          backgroundServerOnCharge = 1;
          wifiAutoConnect = 1;
          break;
        }
        [[fallthrough]];
      case BACKGROUND_SERVER_ON_CHARGE:
        if (supportsBackgroundServerOnChargeMode()) {
          backgroundServerOnCharge = 1;
          wifiAutoConnect = 0;
          break;
        }
        [[fallthrough]];
      case BACKGROUND_SERVER_NEVER:
      default:
        backgroundServerOnCharge = 0;
        wifiAutoConnect = 0;
        break;
    }
  }

  bool keepsBackgroundServerOnWifiWhileAwake() const { return getBackgroundServerMode() == BACKGROUND_SERVER_ALWAYS; }

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  static constexpr uint16_t POWER_BUTTON_WAKE_SHORT_MS = 10;
  static constexpr uint16_t POWER_BUTTON_LONG_PRESS_MS = 400;

  // Wake detection threshold: how long power must be held to trigger sleep/wake.
  uint16_t getPowerButtonWakeDuration() const {
    // Dual-side front layout uses short power taps for Confirm/Back.
    // Keep long-press threshold so short taps are not interpreted as sleep.
    if (frontButtonLayout == CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT) {
      return POWER_BUTTON_LONG_PRESS_MS;
    }
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? POWER_BUTTON_WAKE_SHORT_MS
                                                                    : POWER_BUTTON_LONG_PRESS_MS;
  }

  // Threshold separating short-press from long-press power button in reader.
  uint16_t getPowerButtonLongPressDuration() const { return POWER_BUTTON_LONG_PRESS_MS; }

  // Keep old name as alias so callers outside the reader can be migrated incrementally.
  uint16_t getPowerButtonDuration() const { return getPowerButtonWakeDuration(); }
  int getReaderFontId() const;

  bool saveToFile() const;
  bool loadFromFile();
  static void validateFrontButtonMapping(CrossPointSettings& settings);
  void applyFrontButtonLayoutPreset(FRONT_BUTTON_LAYOUT layout);
  void enforceButtonLayoutConstraints();

  // Validate loaded settings and clamp to valid ranges
  void validateAndClamp();

 private:
  bool loadFromBinaryFile();
  bool migrateLanguageBinaryFile();

 public:
  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
  int getTimeZoneOffsetSeconds() const;
};

// Shared legacy migration helper used by both binary and JSON settings loaders.
void applyLegacyStatusBarSettings(CrossPointSettings& settings);

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
