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
  return rawValue < SLEEP_SCREEN_MODE_COUNT ? rawValue : DARK;
}

void CrossPointSettings::validateAndClamp() {
  sleepScreen = normalizeSleepScreenMode(sleepScreen);
  if (sleepScreenCoverMode > CROP) sleepScreenCoverMode = FIT;
  if (sleepScreenSource >= SLEEP_SCREEN_SOURCE_COUNT) sleepScreenSource = SLEEP_SOURCE_SLEEP;
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
  if (shortPwrBtn > FORCE_REFRESH) shortPwrBtn = IGNORE;
  if (hideBatteryPercentage > HIDE_ALWAYS) hideBatteryPercentage = HIDE_NEVER;
  if (timeMode > TIME_MODE_MANUAL) timeMode = TIME_MODE_UTC;
  if (releaseChannel >= RELEASE_CHANNEL_COUNT) releaseChannel = RELEASE_STABLE;
  if (language >= getLanguageCount()) language = static_cast<uint8_t>(Language::EN);
  if (longPressButtonBehavior >= LONG_PRESS_BUTTON_BEHAVIOR_COUNT) longPressButtonBehavior = CHAPTER_SKIP;
  if (globalStatusBar >= GLOBAL_STATUS_BAR_MODE_COUNT) globalStatusBar = GLOBAL_STATUS_BAR_OFF;
  if (globalStatusBarPosition >= GLOBAL_STATUS_BAR_POSITION_COUNT) globalStatusBarPosition = STATUS_BAR_TOP;

  if (uiTheme > LYRA_CAROUSEL) uiTheme = LYRA;
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
