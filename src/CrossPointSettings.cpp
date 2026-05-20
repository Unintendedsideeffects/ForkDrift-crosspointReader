#include "CrossPointSettings.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>

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
    case CrossPointSettings::SMALL: return NOTOSERIF_12_FONT_ID;
    case CrossPointSettings::LARGE: return NOTOSERIF_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return NOTOSERIF_18_FONT_ID;
    default: return NOTOSERIF_14_FONT_ID;
  }
#elif ENABLE_NOTOSANS_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL: return NOTOSANS_12_FONT_ID;
    case CrossPointSettings::LARGE: return NOTOSANS_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return NOTOSANS_18_FONT_ID;
    default: return NOTOSANS_14_FONT_ID;
  }
#elif ENABLE_LEXENDDECA_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL: return LEXENDDECA_12_FONT_ID;
    case CrossPointSettings::LARGE: return LEXENDDECA_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return LEXENDDECA_18_FONT_ID;
    default: return LEXENDDECA_14_FONT_ID;
  }
#elif ENABLE_BITTER_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL: return BITTER_12_FONT_ID;
    case CrossPointSettings::LARGE: return BITTER_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return BITTER_18_FONT_ID;
    default: return BITTER_14_FONT_ID;
  }
#elif ENABLE_CHAREINK_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL: return CHAREINK_12_FONT_ID;
    case CrossPointSettings::LARGE: return CHAREINK_16_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return CHAREINK_18_FONT_ID;
    default: return CHAREINK_14_FONT_ID;
  }
#elif ENABLE_OPENDYSLEXIC_FONTS
  switch (fontSize) {
    case CrossPointSettings::SMALL: return OPENDYSLEXIC_8_FONT_ID;
    case CrossPointSettings::LARGE: return OPENDYSLEXIC_12_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return OPENDYSLEXIC_14_FONT_ID;
    default: return OPENDYSLEXIC_10_FONT_ID;
  }
#else
  switch (fontSize) {
    case CrossPointSettings::SMALL: return UI_10_FONT_ID;
    case CrossPointSettings::LARGE: return UI_12_FONT_ID;
    case CrossPointSettings::EXTRA_LARGE: return UI_12_FONT_ID;
    default: return UI_10_FONT_ID;
  }
#endif
}

static bool readAndValidate(FsFile& file, uint8_t& member, const uint8_t maxValue) {
  uint8_t tempValue = 0;
  if (!serialization::readPod(file, tempValue)) {
    return false;
  }
  if (tempValue < maxValue) {
    member = tempValue;
  }
  return true;
}

void applyLegacyStatusBarSettings(CrossPointSettings& settings) {
  settings.statusBarProgressBarThickness = CrossPointSettings::PROGRESS_BAR_NORMAL;
  switch (static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
    case CrossPointSettings::NONE:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::NO_PROGRESS:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::ONLY_BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::CHAPTER_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::CHAPTER_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::FULL:
    default:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 4;
// Increment this when adding new persisted settings fields.
// Must match the number of writePod/writeString calls in saveToFile() (excluding the
// version and count header writes). Current count: 39.
constexpr uint8_t SETTINGS_COUNT = 39;
constexpr char SETTINGS_FILE_BIN[] = "/.crosspoint/settings.bin";
constexpr char SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
constexpr char SETTINGS_FILE_BAK[] = "/.crosspoint/settings.bin.bak";
constexpr char LANG_FILE_BIN[] = "/.crosspoint/language.bin";
constexpr char LANG_FILE_BAK[] = "/.crosspoint/language.bin.bak";

void applyLegacyFrontButtonLayout(CrossPointSettings& settings) {
  settings.applyFrontButtonLayoutPreset(
      static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(settings.frontButtonLayout));
}

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

bool CrossPointSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  setDeveloperModeLoggingEnabled(developerMode != 0);
  return JsonSettingsIO::saveSettings(*this, SETTINGS_FILE_JSON);
}

bool CrossPointSettings::loadFromFile() {
  if (Storage.exists(SETTINGS_FILE_JSON)) {
    String json = Storage.readFile(SETTINGS_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadSettings(*this, json.c_str(), &resave);
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
      migrateLanguageBinaryFile();
      return result;
    }
  }

  // Fall back to binary migration
  if (Storage.exists(SETTINGS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      migrateLanguageBinaryFile();
      if (saveToFile()) {
        Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
        LOG_DBG("CPS", "Migrated settings.bin to settings.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save migrated settings to JSON");
      }
    }
  }

  // No settings files at all -- check for standalone language.bin
  return migrateLanguageBinaryFile();
}

bool CrossPointSettings::migrateLanguageBinaryFile() {
  // V1_LANGUAGES / V1_LANGUAGE_COUNT are emitted by gen_i18n.py with the
  // frozen enum order from 2f969a9.
  if (!Storage.exists(LANG_FILE_BIN)) return false;

  FsFile f;
  if (Storage.openFileForRead("CPS", LANG_FILE_BIN, f)) {
    uint8_t version;
    serialization::readPod(f, version);
    if (version == 1) {
      uint8_t oldIndex;
      serialization::readPod(f, oldIndex);
      if (oldIndex < V1_LANGUAGE_COUNT) {
        language = static_cast<uint8_t>(V1_LANGUAGES[oldIndex]);
      }
    }
  }
  Storage.rename(LANG_FILE_BIN, LANG_FILE_BAK);
  saveToFile();
  LOG_DBG("CPS", "Migrated language.bin into settings.json");
  return true;
}

bool CrossPointSettings::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", SETTINGS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version = 0;
  if (!serialization::readPod(inputFile, version)) {
    LOG_ERR("CPS", "Deserialization failed: Could not read version");
    inputFile.close();
    return false;
  }
  if (version < 1 || version > SETTINGS_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  if (!serialization::readPod(inputFile, fileSettingsCount)) {
    LOG_ERR("CPS", "Deserialization failed: Could not read setting count");
    inputFile.close();
    return false;
  }
  if (fileSettingsCount > SETTINGS_COUNT) {
    LOG_WRN("CPS", "Settings count %u exceeds supported %u, truncating", fileSettingsCount, SETTINGS_COUNT);
    fileSettingsCount = SETTINGS_COUNT;
  }

  uint8_t settingsRead = 0;
  bool frontButtonMappingRead = false;
  do {
    serialization::readPod(inputFile, sleepScreen);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);  // legacy
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);  // legacy
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, fontFamily, FONT_FAMILY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, fontSize, FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(sleepTimeout);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, longPressButtonBehavior, LONG_PRESS_BUTTON_BEHAVIOR_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, backgroundServerOnCharge);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, todoFallbackCover);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, timeMode);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, timeZoneOffset);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, lastTimeSyncEpoch);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, releaseChannel, RELEASE_CHANNEL_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenSource, SLEEP_SCREEN_SOURCE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    if (version >= 2) {
      std::string pathStr;
      serialization::readString(inputFile, pathStr);
      strncpy(userFontPath, pathStr.c_str(), sizeof(userFontPath) - 1);
      userFontPath[sizeof(userFontPath) - 1] = '\0';
      if (++settingsRead >= fileSettingsCount) break;
    }
    if (version >= 3) {
      serialization::readPod(inputFile, usbMscPromptOnConnect);
      if (++settingsRead >= fileSettingsCount) break;
    }
    if (version >= 4) {
      std::string selectedOtaBundleStr;
      serialization::readString(inputFile, selectedOtaBundleStr);
      strncpy(selectedOtaBundle, selectedOtaBundleStr.c_str(), sizeof(selectedOtaBundle) - 1);
      selectedOtaBundle[sizeof(selectedOtaBundle) - 1] = '\0';
      if (++settingsRead >= fileSettingsCount) break;

      std::string installedOtaBundleStr;
      serialization::readString(inputFile, installedOtaBundleStr);
      strncpy(installedOtaBundle, installedOtaBundleStr.c_str(), sizeof(installedOtaBundle) - 1);
      installedOtaBundle[sizeof(installedOtaBundle) - 1] = '\0';
      if (++settingsRead >= fileSettingsCount) break;

      std::string installedOtaFeatureFlagsStr;
      serialization::readString(inputFile, installedOtaFeatureFlagsStr);
      strncpy(installedOtaFeatureFlags, installedOtaFeatureFlagsStr.c_str(), sizeof(installedOtaFeatureFlags) - 1);
      installedOtaFeatureFlags[sizeof(installedOtaFeatureFlags) - 1] = '\0';
      if (++settingsRead >= fileSettingsCount) break;
    }

    const bool backRead = readAndValidate(inputFile, frontButtonBack, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    const bool confirmRead = readAndValidate(inputFile, frontButtonConfirm, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    const bool leftRead = readAndValidate(inputFile, frontButtonLeft, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    const bool rightRead = readAndValidate(inputFile, frontButtonRight, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    frontButtonMappingRead = backRead && confirmRead && leftRead && rightRead;
  } while (false);

  if (frontButtonMappingRead) {
    CrossPointSettings::validateFrontButtonMapping(*this);
  } else {
    applyLegacyFrontButtonLayout(*this);
  }

  // Binary settings only carry the legacy statusBar enum.
  applyLegacyStatusBarSettings(*this);

  validateAndClamp();
  inputFile.close();
  LOG_DBG("CPS", "Settings loaded from binary file");
  return true;
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
  if (sleepTimeout > SLEEP_30_MIN) sleepTimeout = SLEEP_10_MIN;
  if (refreshFrequency > REFRESH_30) refreshFrequency = REFRESH_15;
  if (shortPwrBtn > FORCE_REFRESH) shortPwrBtn = IGNORE;
  if (hideBatteryPercentage > HIDE_ALWAYS) hideBatteryPercentage = HIDE_NEVER;
  if (timeMode > TIME_MODE_MANUAL) timeMode = TIME_MODE_UTC;
  if (todoFallbackCover > 1) todoFallbackCover = 0;
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

  if (timeZoneOffset > 26) timeZoneOffset = 12;
  if (screenMargin < 5 || screenMargin > 40) screenMargin = 5;

  extraParagraphSpacing = extraParagraphSpacing ? 1 : 0;
  textAntiAliasing = textAntiAliasing ? 1 : 0;
  hyphenationEnabled = hyphenationEnabled ? 1 : 0;
  longPressChapterSkip = (longPressButtonBehavior == CHAPTER_SKIP) ? 1 : 0;
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
  // SD card fonts use same compression as Bookerly (the most neutral values)
  if (sdFontFamilyName[0] != '\0') {
    switch (lineSpacing) {
      case TIGHT:
        return 0.95f;
      case NORMAL:
      default:
        return 1.0f;
      case WIDE:
        return 1.1f;
    }
  }

  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
    case NOTOSANS:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case OPENDYSLEXIC:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case LEXENDDECA:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case BITTER:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
    case CHAREINK:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case USER_SD:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
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
    default:
#if ENABLE_BOOKERLY_FONTS
      switch (fontSize) {
        case SMALL: return NOTOSERIF_12_FONT_ID;
        case LARGE: return NOTOSERIF_16_FONT_ID;
        case EXTRA_LARGE: return NOTOSERIF_18_FONT_ID;
        default: return NOTOSERIF_14_FONT_ID;
      }
#else
      return getDefaultFontId(fontSize);
#endif
    case NOTOSANS:
#if ENABLE_NOTOSANS_FONTS
      switch (fontSize) {
        case SMALL: return NOTOSANS_12_FONT_ID;
        case LARGE: return NOTOSANS_16_FONT_ID;
        case EXTRA_LARGE: return NOTOSANS_18_FONT_ID;
        default: return NOTOSANS_14_FONT_ID;
      }
#else
      return getDefaultFontId(fontSize);
#endif
    case OPENDYSLEXIC:
#if ENABLE_OPENDYSLEXIC_FONTS
      switch (fontSize) {
        case SMALL: return OPENDYSLEXIC_8_FONT_ID;
        case LARGE: return OPENDYSLEXIC_12_FONT_ID;
        case EXTRA_LARGE: return OPENDYSLEXIC_14_FONT_ID;
        default: return OPENDYSLEXIC_10_FONT_ID;
      }
#else
      return getDefaultFontId(fontSize);
#endif
    case LEXENDDECA:
#if ENABLE_LEXENDDECA_FONTS
      switch (fontSize) {
        case SMALL: return LEXENDDECA_12_FONT_ID;
        case LARGE: return LEXENDDECA_16_FONT_ID;
        case EXTRA_LARGE: return LEXENDDECA_18_FONT_ID;
        default: return LEXENDDECA_14_FONT_ID;
      }
#else
      return getDefaultFontId(fontSize);
#endif
    case BITTER:
#if ENABLE_BITTER_FONTS
      switch (fontSize) {
        case SMALL: return BITTER_12_FONT_ID;
        case LARGE: return BITTER_16_FONT_ID;
        case EXTRA_LARGE: return BITTER_18_FONT_ID;
        default: return BITTER_14_FONT_ID;
      }
#else
      return getDefaultFontId(fontSize);
#endif
    case CHAREINK:
#if ENABLE_CHAREINK_FONTS
      switch (fontSize) {
        case SMALL: return CHAREINK_12_FONT_ID;
        case LARGE: return CHAREINK_16_FONT_ID;
        case EXTRA_LARGE: return CHAREINK_18_FONT_ID;
        default: return CHAREINK_14_FONT_ID;
      }
#else
      return getDefaultFontId(fontSize);
#endif
  }
}
