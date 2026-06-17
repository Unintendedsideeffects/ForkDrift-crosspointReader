#include "CrossPointSettings.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "FeatureFlags.h"
#include "I18nKeys.h"
#include "fontIds.h"

CrossPointSettings CrossPointSettings::instance;

// Compile-time first-available font family — used as the fallback when a
// stored font family value is disabled in the current build.
#if ENABLE_BOOKERLY_FONTS
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::BOOKERLY;
#elif ENABLE_NOTOSANS_FONTS
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::NOTOSANS;
#elif ENABLE_LEXENDDECA_FONTS
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::LEXENDDECA;
#elif ENABLE_BITTER_FONTS
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::BITTER;
#elif ENABLE_CHAREINK_FONTS
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::CHAREINK;
#elif ENABLE_OPENDYSLEXIC_FONTS
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::OPENDYSLEXIC;
#else
static constexpr uint8_t kFirstAvailableFont = CrossPointSettings::BOOKERLY;
#endif

static int getDefaultFontId(uint8_t fontSize) {
#if ENABLE_BOOKERLY_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return NOTOSERIF_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return NOTOSERIF_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return NOTOSERIF_18_FONT_ID;
    default:
      return NOTOSERIF_14_FONT_ID;
  }
#elif ENABLE_NOTOSANS_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return NOTOSANS_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return NOTOSANS_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return NOTOSANS_18_FONT_ID;
    default:
      return NOTOSANS_14_FONT_ID;
  }
#elif ENABLE_LEXENDDECA_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return LEXENDDECA_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return LEXENDDECA_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return LEXENDDECA_18_FONT_ID;
    default:
      return LEXENDDECA_14_FONT_ID;
  }
#elif ENABLE_BITTER_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return BITTER_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return BITTER_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return BITTER_18_FONT_ID;
    default:
      return BITTER_14_FONT_ID;
  }
#elif ENABLE_CHAREINK_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return CHAREINK_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return CHAREINK_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return CHAREINK_18_FONT_ID;
    default:
      return CHAREINK_14_FONT_ID;
  }
#elif ENABLE_OPENDYSLEXIC_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return OPENDYSLEXIC_8_FONT_ID;
    case CrossPointSettings::LARGE:
      return OPENDYSLEXIC_12_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return OPENDYSLEXIC_14_FONT_ID;
    default:
      return OPENDYSLEXIC_10_FONT_ID;
  }
#else
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return UI_10_FONT_ID;
    case CrossPointSettings::LARGE:
      return UI_12_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return UI_12_FONT_ID;
    default:
      return UI_10_FONT_ID;
  }
#endif
}

static float compressionForLineSpacing(uint8_t lineSpacing, float tight, float normal, float wide) {
  switch (lineSpacing) {
    case CrossPointSettings::TIGHT:
      return tight;
    case CrossPointSettings::WIDE:
      return wide;
    case CrossPointSettings::NORMAL:
    default:
      return normal;
  }
}

static int bookerlyReaderFontId(uint8_t fontSize) {
#if ENABLE_BOOKERLY_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return NOTOSERIF_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return NOTOSERIF_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return NOTOSERIF_18_FONT_ID;
    default:
      return NOTOSERIF_14_FONT_ID;
  }
#else
  return getDefaultFontId(fontSize);
#endif
}

static int notoSansReaderFontId(uint8_t fontSize) {
#if ENABLE_NOTOSANS_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return NOTOSANS_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return NOTOSANS_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return NOTOSANS_18_FONT_ID;
    default:
      return NOTOSANS_14_FONT_ID;
  }
#else
  return getDefaultFontId(fontSize);
#endif
}

static int openDyslexicReaderFontId(uint8_t fontSize) {
#if ENABLE_OPENDYSLEXIC_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return OPENDYSLEXIC_8_FONT_ID;
    case CrossPointSettings::LARGE:
      return OPENDYSLEXIC_12_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return OPENDYSLEXIC_14_FONT_ID;
    default:
      return OPENDYSLEXIC_10_FONT_ID;
  }
#else
  return getDefaultFontId(fontSize);
#endif
}

static int lexendDecaReaderFontId(uint8_t fontSize) {
#if ENABLE_LEXENDDECA_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return LEXENDDECA_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return LEXENDDECA_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return LEXENDDECA_18_FONT_ID;
    default:
      return LEXENDDECA_14_FONT_ID;
  }
#else
  return getDefaultFontId(fontSize);
#endif
}

static int bitterReaderFontId(uint8_t fontSize) {
#if ENABLE_BITTER_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return BITTER_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return BITTER_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return BITTER_18_FONT_ID;
    default:
      return BITTER_14_FONT_ID;
  }
#else
  return getDefaultFontId(fontSize);
#endif
}

static int chareInkReaderFontId(uint8_t fontSize) {
#if ENABLE_CHAREINK_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return CHAREINK_12_FONT_ID;
    case CrossPointSettings::LARGE:
      return CHAREINK_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE:
      return CHAREINK_18_FONT_ID;
    default:
      return CHAREINK_14_FONT_ID;
  }
#else
  return getDefaultFontId(fontSize);
#endif
}

namespace {
constexpr char SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

bool CrossPointSettings::resetToDefaults() {
  instance.~CrossPointSettings();
  new (&instance) CrossPointSettings();
  instance.validateAndClamp();
  return instance.saveToFile();
}

bool CrossPointSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  setDeveloperModeLoggingEnabled(developerMode != 0);
  return JsonSettingsIO::saveSettings(*this, SETTINGS_FILE_JSON);
}

bool CrossPointSettings::loadFromFile() {
  if (Storage.exists(SETTINGS_FILE_JSON)) {
    HalFile file;
    if (Storage.openFileForRead("CPS", SETTINGS_FILE_JSON, file)) {
      bool resave = false;
      const bool result = JsonSettingsIO::loadSettings(*this, file, &resave);
      file.close();
      if (result) {
        validateAndClamp();
        if (resave) {
          if (saveToFile()) {
            LOG_DBG("CPS", "Resaved settings to update format");
          } else {
            LOG_ERR("CPS", "Failed to resave settings after format update");
          }
        }
      }
      return result;
    }
  }

  return false;
}

static bool isShortPowerButtonActionSupported(const uint8_t action) {
  using S = CrossPointSettings;
  switch (action) {
    case S::TOGGLE_GUIDE_DOTS:
#if ENABLE_GUIDE_DOTS
      return true;
#else
      return false;
#endif
    case S::TOGGLE_BIONIC_READING:
#if ENABLE_FOCUS_READING
      return true;
#else
      return false;
#endif
    case S::TOGGLE_BOOKMARK:
#if ENABLE_BOOKMARKS
      return true;
#else
      return false;
#endif
    case S::SYNC_PROGRESS:
#if ENABLE_KOREADER_SYNC
      return true;
#else
      return false;
#endif
    case S::MARK_FINISHED:
    case S::READING_STATS:
#if ENABLE_READING_STATS
      return true;
#else
      return false;
#endif
    case S::FILE_TRANSFER:
#if ENABLE_USB_MASS_STORAGE
      return true;
#else
      return false;
#endif
    default:
      return action < S::SHORT_PWRBTN_COUNT;
  }
}

static bool isLongPressMenuActionSupported(const uint8_t action) {
  using S = CrossPointSettings;
  switch (action) {
    case S::LONG_MENU_TOGGLE_GUIDE_DOTS:
#if ENABLE_GUIDE_DOTS
      return true;
#else
      return false;
#endif
    case S::LONG_MENU_TOGGLE_BIONIC:
#if ENABLE_FOCUS_READING
      return true;
#else
      return false;
#endif
    case S::LONG_MENU_TOGGLE_BOOKMARK:
#if ENABLE_BOOKMARKS
      return true;
#else
      return false;
#endif
    case S::LONG_MENU_SYNC_PROGRESS:
#if ENABLE_KOREADER_SYNC
      return true;
#else
      return false;
#endif
    case S::LONG_MENU_MARK_FINISHED:
    case S::LONG_MENU_READING_STATS:
#if ENABLE_READING_STATS
      return true;
#else
      return false;
#endif
    case S::LONG_MENU_FILE_TRANSFER:
#if ENABLE_USB_MASS_STORAGE
      return true;
#else
      return false;
#endif
    default:
      return action < S::LONG_PRESS_MENU_ACTION_COUNT;
  }
}

void CrossPointSettings::applyFrontButtonLayoutPreset(const FRONT_BUTTON_LAYOUT layout) {
  frontButtonLayout = static_cast<uint8_t>(layout);

  switch (layout) {
    case LEFT_RIGHT_BACK_CONFIRM:
      frontButtonBack = FRONT_HW_LEFT;
      frontButtonConfirm = FRONT_HW_RIGHT;
      frontButtonLeft = FRONT_HW_BACK;
      frontButtonRight = FRONT_HW_CONFIRM;
      break;
    case LEFT_BACK_CONFIRM_RIGHT:
      frontButtonBack = FRONT_HW_CONFIRM;
      frontButtonConfirm = FRONT_HW_LEFT;
      frontButtonLeft = FRONT_HW_BACK;
      frontButtonRight = FRONT_HW_RIGHT;
      break;
    case BACK_CONFIRM_RIGHT_LEFT:
      frontButtonBack = FRONT_HW_BACK;
      frontButtonConfirm = FRONT_HW_CONFIRM;
      frontButtonLeft = FRONT_HW_RIGHT;
      frontButtonRight = FRONT_HW_LEFT;
      break;
    case LEFT_LEFT_RIGHT_RIGHT:
    case BACK_CONFIRM_LEFT_RIGHT:
    default:
      frontButtonBack = FRONT_HW_BACK;
      frontButtonConfirm = FRONT_HW_CONFIRM;
      frontButtonLeft = FRONT_HW_LEFT;
      frontButtonRight = FRONT_HW_RIGHT;
      break;
  }
}

void CrossPointSettings::enforceButtonLayoutConstraints() {
  if (frontButtonLayout == LEFT_LEFT_RIGHT_RIGHT) {
    shortPwrBtn = SELECT;
  }
}

uint8_t CrossPointSettings::normalizeSleepScreenMode(const uint8_t rawValue) {
  if (rawValue == COVER) return COVER;
  if (rawValue == SMART) return DARK;
  if (rawValue == COVER_CUSTOM) {
    return CUSTOM;
  }
  if (rawValue == 6 /* old TRANSPARENT */) {
    return TRANSPARENT;
  }
#if ENABLE_READING_STATS
  if (rawValue == READING_STATS_SLEEP) {
    return READING_STATS_SLEEP;
  }
#else
  if (rawValue == READING_STATS_SLEEP) {
    return DARK;
  }
#endif
#if ENABLE_ROMAN_CLOCK_SLEEP
  if (rawValue == ROMAN_CLOCK_SLEEP) {
    return ROMAN_CLOCK_SLEEP;
  }
#else
  if (rawValue == ROMAN_CLOCK_SLEEP) {
    return DARK;
  }
#endif
#if ENABLE_HAIKU_CLOCK
  if (rawValue == HAIKU_CLOCK_SLEEP) {
    return HAIKU_CLOCK_SLEEP;
  }
#else
  if (rawValue == HAIKU_CLOCK_SLEEP) {
    return DARK;
  }
#endif
#if ENABLE_NOTES
  if (rawValue == NOTES_SLEEP) {
    return NOTES_SLEEP;
  }
#else
  if (rawValue == NOTES_SLEEP) {
    return DARK;
  }
#endif
#if ENABLE_TODO_PLANNER
  if (rawValue == PLANNER_SLEEP) {
    return PLANNER_SLEEP;
  }
#else
  if (rawValue == PLANNER_SLEEP) {
    return DARK;
  }
#endif
  return rawValue < SLEEP_SCREEN_MODE_COUNT ? rawValue : DARK;
}

void CrossPointSettings::validateAndClamp() {
  sleepScreen = normalizeSleepScreenMode(sleepScreen);
  if (sleepScreenCoverMode > CROP) sleepScreenCoverMode = FIT;
  if (sleepScreenSource >= SLEEP_SCREEN_SOURCE_COUNT) sleepScreenSource = SLEEP_SOURCE_SLEEP;
  if (sleepScreenSplit >= SLEEP_SCREEN_SPLIT_COUNT) sleepScreenSplit = SLEEP_SPLIT_UNIFIED;
  sleepScreenReader = normalizeSleepScreenMode(sleepScreenReader);
  sleepScreenHome = normalizeSleepScreenMode(sleepScreenHome);
  if (statusBar >= STATUS_BAR_MODE_COUNT) statusBar = FULL;
  if (statusBarProgressBar >= STATUS_BAR_PROGRESS_BAR_COUNT) statusBarProgressBar = HIDE_PROGRESS;
  if (statusBarProgressBarThickness >= STATUS_BAR_PROGRESS_BAR_THICKNESS_COUNT) {
    statusBarProgressBarThickness = PROGRESS_BAR_NORMAL;
  }
  if (statusBarTitle >= STATUS_BAR_TITLE_COUNT) statusBarTitle = CHAPTER_TITLE;
  if (orientation > LANDSCAPE_CCW) orientation = PORTRAIT;
  if (frontButtonLayout > LEFT_LEFT_RIGHT_RIGHT) frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  if (sideButtonLayout > NEXT_PREV) sideButtonLayout = PREV_NEXT;
  if (fontFamily >= FONT_FAMILY_COUNT) fontFamily = kFirstAvailableFont;
  if (fontSize > EXTRA_LARGE) fontSize = MEDIUM;
#if !ENABLE_BOOKERLY_FONTS
  if (fontFamily == BOOKERLY) fontFamily = kFirstAvailableFont;
#endif
#if !ENABLE_NOTOSANS_FONTS
  if (fontFamily == NOTOSANS) fontFamily = kFirstAvailableFont;
#endif
#if !ENABLE_OPENDYSLEXIC_FONTS
  if (fontFamily == OPENDYSLEXIC) fontFamily = kFirstAvailableFont;
#endif
#if !ENABLE_LEXENDDECA_FONTS
  if (fontFamily == LEXENDDECA) fontFamily = kFirstAvailableFont;
#endif
#if !ENABLE_BITTER_FONTS
  if (fontFamily == BITTER) fontFamily = kFirstAvailableFont;
#endif
#if !ENABLE_CHAREINK_FONTS
  if (fontFamily == CHAREINK) fontFamily = kFirstAvailableFont;
#endif
  if (fontFamily == USER_SD) fontFamily = kFirstAvailableFont;
  if (lineSpacing > WIDE) lineSpacing = NORMAL;
  if (paragraphAlignment >= PARAGRAPH_ALIGNMENT_COUNT) paragraphAlignment = JUSTIFIED;
  if (refreshFrequency > REFRESH_30) refreshFrequency = REFRESH_15;
  if (shortPwrBtn >= SHORT_PWRBTN_COUNT || !isShortPowerButtonActionSupported(shortPwrBtn)) shortPwrBtn = IGNORE;
  if (longPwrBtn >= SHORT_PWRBTN_COUNT || !isShortPowerButtonActionSupported(longPwrBtn)) longPwrBtn = IGNORE;
#if ENABLE_DOUBLE_TAP_ACTION
  if (doubleTapPwrBtn >= SHORT_PWRBTN_COUNT || !isShortPowerButtonActionSupported(doubleTapPwrBtn))
    doubleTapPwrBtn = FORCE_REFRESH;
#endif
  if (longPressMenuAction >= LONG_PRESS_MENU_ACTION_COUNT || !isLongPressMenuActionSupported(longPressMenuAction)) {
    longPressMenuAction = LONG_MENU_OFF;
  }
  if (hideBatteryPercentage > HIDE_ALWAYS) hideBatteryPercentage = HIDE_NEVER;
  if (timeMode > TIME_MODE_MANUAL) timeMode = TIME_MODE_UTC;
  if (releaseChannel >= RELEASE_CHANNEL_COUNT) releaseChannel = RELEASE_STABLE;
  if (language >= getLanguageCount()) language = static_cast<uint8_t>(Language::EN);
  if (longPressButtonBehavior >= LONG_PRESS_BUTTON_BEHAVIOR_COUNT) longPressButtonBehavior = CHAPTER_SKIP;
  if (globalStatusBar >= GLOBAL_STATUS_BAR_MODE_COUNT) globalStatusBar = GLOBAL_STATUS_BAR_ON;
  if (globalStatusBarPosition >= GLOBAL_STATUS_BAR_POSITION_COUNT) globalStatusBarPosition = STATUS_BAR_TOP;

#if ENABLE_FLOW_THEME
  if (uiTheme > FLOW) uiTheme = LYRA;
#else
  if (uiTheme > TERMINAL) uiTheme = LYRA;
#endif
  if (recentBooksView >= RECENT_BOOKS_VIEW_COUNT) recentBooksView = RECENT_BOOKS_LIST;
#if !ENABLE_LYRA_THEME
  uiTheme = CLASSIC;
#elif !ENABLE_MINIMAL_THEME
  if (uiTheme == MINIMAL) uiTheme = LYRA;
#endif
#if !ENABLE_FOCUS_READING
  focusReadingEnabled = 0;
#endif
#if !ENABLE_GUIDE_DOTS
  guideReadingEnabled = 0;
#endif

  if (timeZoneOffset > 26) timeZoneOffset = 12;
  if (screenMargin < 5 || screenMargin > 40) screenMargin = 5;

  extraParagraphSpacing = extraParagraphSpacing ? 1 : 0;
  textAntiAliasing = textAntiAliasing ? 1 : 0;
  hyphenationEnabled = hyphenationEnabled ? 1 : 0;
  showButtonHints = showButtonHints ? 1 : 0;
  statusBarChapterPageCount = statusBarChapterPageCount ? 1 : 0;
  statusBarBookProgressPercentage = statusBarBookProgressPercentage ? 1 : 0;
  statusBarBattery = statusBarBattery ? 1 : 0;
  backgroundServerOnCharge = backgroundServerOnCharge ? 1 : 0;
  wifiAutoConnect = wifiAutoConnect ? 1 : 0;
  if (!supportsBackgroundServerOnChargeMode()) {
    backgroundServerOnCharge = 0;
  }
  if (!supportsBackgroundServerAlwaysMode()) {
    wifiAutoConnect = 0;
  }
  if (wifiAutoConnect) {
    backgroundServerOnCharge = 1;
  }
  usbMscPromptOnConnect = usbMscPromptOnConnect ? 1 : 0;
  developerMode = developerMode ? 1 : 0;
  setDeveloperModeLoggingEnabled(developerMode != 0);

  // Sanitize deviceName: keep only [a-z0-9-], lowercase, max 24 usable chars.
  {
    char sanitized[25] = {};
    size_t out = 0;
    for (size_t i = 0; deviceName[i] != '\0' && out < 24; ++i) {
      const char c = static_cast<char>(tolower(static_cast<unsigned char>(deviceName[i])));
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
        sanitized[out++] = c;
      }
    }
    // Strip leading/trailing hyphens
    size_t start = 0;
    while (start < out && sanitized[start] == '-') ++start;
    while (out > start && sanitized[out - 1] == '-') --out;
    memmove(sanitized, sanitized + start, out - start);
    out -= start;
    sanitized[out] = '\0';
    strncpy(deviceName, sanitized, sizeof(deviceName) - 1);
    deviceName[sizeof(deviceName) - 1] = '\0';
  }

  enforceButtonLayoutConstraints();
}

bool CrossPointSettings::sleepModeActive(uint8_t mode) {
  if (SETTINGS.sleepScreenSplit == SLEEP_SPLIT_SMART) {
    return SETTINGS.sleepScreenReader == mode || SETTINGS.sleepScreenHome == mode;
  }
  return SETTINGS.sleepScreen == mode;
}

float CrossPointSettings::getReaderLineCompression() const {
  if (sdFontFamilyName[0] != '\0') {
    return compressionForLineSpacing(lineSpacing, 0.95f, 1.0f, 1.1f);
  }

  switch (fontFamily) {
    case NOTOSANS:
    case OPENDYSLEXIC:
    case LEXENDDECA:
    case CHAREINK:
      return compressionForLineSpacing(lineSpacing, 0.90f, 0.95f, 1.0f);
    case USER_SD:
      return compressionForLineSpacing(lineSpacing, 0.90f, 1.0f, 1.1f);
    case BOOKERLY:
    case BITTER:
    default:
      return compressionForLineSpacing(lineSpacing, 0.95f, 1.0f, 1.1f);
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  const uint8_t minutes = std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, MAX_SLEEP_TIMEOUT_MINUTES);
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

uint64_t CrossPointSettings::getTimedRefreshIntervalMicros() const {
  // Interval values: 0=off, 1=1h, 2=2h, 3=4h, 4=8h, 5=24h
  static constexpr uint64_t kHour = 3600ULL * 1000000ULL;
  static constexpr uint64_t kHours[] = {0, 1, 2, 4, 8, 24};
  if (timedSleepRefreshInterval >= sizeof(kHours) / sizeof(kHours[0])) return 0;
  return kHours[timedSleepRefreshInterval] * kHour;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

int CrossPointSettings::getTimeZoneOffsetSeconds() const {
  const int offsetHours = static_cast<int>(timeZoneOffset) - 12;
  return offsetHours * 3600;
}

int CrossPointSettings::getReaderFontId() const {
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    const int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontSize);
    if (id != 0) return id;
  }

  uint8_t effectiveFamily = fontFamily;
#if !ENABLE_BOOKERLY_FONTS
  if (effectiveFamily == BOOKERLY) effectiveFamily = kFirstAvailableFont;
#endif
#if !ENABLE_NOTOSANS_FONTS
  if (effectiveFamily == NOTOSANS) effectiveFamily = kFirstAvailableFont;
#endif
#if !ENABLE_OPENDYSLEXIC_FONTS
  if (effectiveFamily == OPENDYSLEXIC) effectiveFamily = kFirstAvailableFont;
#endif
#if !ENABLE_LEXENDDECA_FONTS
  if (effectiveFamily == LEXENDDECA) effectiveFamily = kFirstAvailableFont;
#endif
#if !ENABLE_BITTER_FONTS
  if (effectiveFamily == BITTER) effectiveFamily = kFirstAvailableFont;
#endif
#if !ENABLE_CHAREINK_FONTS
  if (effectiveFamily == CHAREINK) effectiveFamily = kFirstAvailableFont;
#endif
  if (effectiveFamily == USER_SD) effectiveFamily = kFirstAvailableFont;

  switch (effectiveFamily) {
    case NOTOSANS:
      return notoSansReaderFontId(fontSize);
    case OPENDYSLEXIC:
      return openDyslexicReaderFontId(fontSize);
    case LEXENDDECA:
      return lexendDecaReaderFontId(fontSize);
    case BITTER:
      return bitterReaderFontId(fontSize);
    case CHAREINK:
      return chareInkReaderFontId(fontSize);
    default:
      return bookerlyReaderFontId(fontSize);
  }
}

namespace {
std::string base64Encode(const uint8_t* data, size_t len) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t val = (data[i] << 16);
    if (i + 1 < len) val |= (data[i + 1] << 8);
    if (i + 2 < len) val |= data[i + 2];

    result.push_back(alphabet[(val >> 18) & 0x3F]);
    result.push_back(alphabet[(val >> 12) & 0x3F]);
    result.push_back((i + 1 < len) ? alphabet[(val >> 6) & 0x3F] : '=');
    result.push_back((i + 2 < len) ? alphabet[val & 0x3F] : '=');
  }
  return result;
}
}  // namespace

std::string CrossPointSettings::getCondensedSettings() const {
  uint8_t buffer[64];
  buffer[0] = 0x01;  // Version 1

  buffer[1] = sleepScreen;
  buffer[2] = sleepScreenCoverMode;
  buffer[3] = sleepScreenCoverFilter;
  buffer[4] = sleepScreenSource;
  buffer[5] = sleepCycleMode;
  buffer[6] = statusBar;
  buffer[7] = statusBarChapterPageCount;
  buffer[8] = statusBarBookProgressPercentage;
  buffer[9] = statusBarProgressBar;
  buffer[10] = statusBarProgressBarThickness;
  buffer[11] = statusBarTitle;
  buffer[12] = statusBarBattery;
  buffer[13] = statusBarClock;
  buffer[14] = clockUtcOffsetQ;
  buffer[15] = clockFormat;
  buffer[16] = clockHasBeenSynced;
  buffer[17] = extraParagraphSpacing;
  buffer[18] = forceParagraphIndents;
  buffer[19] = textAntiAliasing;
  buffer[20] = shortPwrBtn;
  buffer[21] = orientation;
  buffer[22] = frontButtonLayout;
  buffer[23] = sideButtonLayout;
  buffer[24] = frontButtonOrientationAware;
  buffer[25] = sideButtonOrientationAware;
  buffer[26] = sideButtonLongPress;
  buffer[27] = frontButtonBack;
  buffer[28] = frontButtonConfirm;
  buffer[29] = frontButtonLeft;
  buffer[30] = frontButtonRight;
  buffer[31] = fontFamily;
  buffer[32] = fontSize;
  buffer[33] = lineSpacing;
  buffer[34] = paragraphAlignment;
  buffer[35] = sleepTimeoutMinutes;
  buffer[36] = refreshFrequency;
  buffer[37] = hyphenationEnabled;
  buffer[38] = screenMargin;
  buffer[39] = hideBatteryPercentage;
  buffer[40] = longPressButtonBehavior;
  buffer[41] = uiTheme;
  buffer[42] = recentBooksView;
  buffer[43] = fadingFix;
  buffer[44] = embeddedStyle;
  buffer[45] = backgroundServerOnCharge;
  buffer[46] = timeMode;
  buffer[47] = timeZoneOffset;
  buffer[48] = releaseChannel;
  buffer[49] = darkMode;
  buffer[50] = usbMscPromptOnConnect;
  buffer[51] = wifiAutoConnect;
  buffer[52] = focusReadingEnabled;
  buffer[53] = guideReadingEnabled;
  buffer[54] = showHiddenFiles;
  buffer[55] = todoOpenDirectToToday;
  buffer[56] = moveFinishedToReadFolder;
  buffer[57] = developerMode;
  buffer[58] = imageRendering;
  buffer[59] = longPwrBtn;
  buffer[60] = longPressMenuAction;
  buffer[61] = globalStatusBar;
  buffer[62] = globalStatusBarPosition;
  buffer[63] = language;

  return base64Encode(buffer, sizeof(buffer));
}
