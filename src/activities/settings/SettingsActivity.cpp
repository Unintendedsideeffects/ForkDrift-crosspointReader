#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "FactoryResetActivity.h"
#include "FontDownloadActivity.h"
#include "FontSelectionActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "ValidateSleepImagesActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"

namespace {
constexpr char kBackgroundServerModeKey[] = "backgroundServerMode";
}

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  for (auto& setting : getSettingsList(&sdFontSystem.registry())) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
  }

  // Append device-only ACTION items
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  if (core::FeatureModules::supportsSettingAction(SettingAction::CheckForUpdates)) {
    systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_FACTORY_RESET, SettingAction::FactoryReset));
  if (core::FeatureModules::supportsSettingAction(SettingAction::Language)) {
    systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  }
  if (!readerSettings.empty()) {
    readerSettings.insert(readerSettings.begin() + 1,
                          SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  }
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  // Update currentSettings pointer and count for the active category
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;

  rebuildSettingsLists();

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  bool hasChangedCategory = false;

  // Handle actions with early return
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      hasChangedCategory = true;
      requestUpdate();
    } else {
      toggleCurrentSetting();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
    } else {
      if (!SETTINGS.saveToFile()) {
        LOG_WRN("SETTINGS", "Failed to persist settings to SD card");
      }
      activityManager.goHome();
    }
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedCategory] {
    hasChangedCategory = true;
    selectedCategoryIndex = ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
    requestUpdate();
  });

  if (hasChangedCategory) {
    selectedSettingIndex = (selectedSettingIndex == 0) ? 0 : 1;
    switch (selectedCategoryIndex) {
      case 0:
        currentSettings = &displaySettings;
        break;
      case 1:
        currentSettings = &readerSettings;
        break;
      case 2:
        currentSettings = &controlsSettings;
        break;
      case 3:
        currentSettings = &systemSettings;
        break;
    }
    settingsCount = static_cast<int>(currentSettings->size());
  }
}

void SettingsActivity::enterCategory(int categoryIndex) {
  if (selectedSettingIndex > 0) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  if (categoryIndex < 0 || categoryIndex >= categoryCount) {
    return;
  }

  selectedCategoryIndex = categoryIndex;
  selectedSettingIndex = 1;

  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
    default:
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());

  requestUpdate();
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  const auto persistSettings = [this] {
    SETTINGS.enforceButtonLayoutConstraints();
    renderer.setDarkMode(SETTINGS.darkMode);
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("SETTINGS", "Failed to persist settings to SD card");
    }
  };

  // Sleep source only applies when custom sleep screen mode is enabled.
  if (setting.valuePtr == &CrossPointSettings::sleepScreenSource &&
      SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    return;
  }

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) {
                               core::FeatureModules::onFontFamilySettingChanged(SETTINGS.fontFamily);
                               SETTINGS.saveToFile();
                               rebuildSettingsLists();
                               requestUpdate();
                             });
      return;
    }

    std::vector<std::string> values;
    if (!setting.enumStringValues.empty()) {
      values = setting.enumStringValues;
    } else if (setting.dynamicValuesGetter) {
      values = setting.dynamicValuesGetter();
    } else {
      values.reserve(setting.enumValues.size());
      std::transform(setting.enumValues.begin(), setting.enumValues.end(), std::back_inserter(values),
                     [](StrId id) { return std::string(I18N.get(id)); });
    }
    if (values.empty()) {
      return;
    }
    const uint8_t currentValue = (setting.valueGetter) ? setting.valueGetter() : SETTINGS.*(setting.valuePtr);
    const uint8_t maxIndex = static_cast<uint8_t>(values.size() - 1);
    const uint8_t normalizedValue = (currentValue > maxIndex) ? 0 : currentValue;
    const uint8_t newValue = (normalizedValue + 1) % static_cast<uint8_t>(values.size());
    const auto applyEnumValue = [this](const SettingInfo& targetSetting, const uint8_t value) {
      if (targetSetting.valueSetter) {
        targetSetting.valueSetter(value);
      } else if (targetSetting.valuePtr) {
        SETTINGS.*(targetSetting.valuePtr) = value;
      }

      if (targetSetting.valuePtr == &CrossPointSettings::frontButtonLayout) {
        SETTINGS.applyFrontButtonLayoutPreset(
            static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout));
      }

      if (targetSetting.valuePtr == &CrossPointSettings::fontFamily) {
        core::FeatureModules::onFontFamilySettingChanged(value);
      }
    };
    const bool requiresBatteryWarning = setting.key != nullptr && strcmp(setting.key, kBackgroundServerModeKey) == 0 &&
                                        normalizedValue != CrossPointSettings::BACKGROUND_SERVER_ALWAYS &&
                                        newValue == CrossPointSettings::BACKGROUND_SERVER_ALWAYS;

    if (requiresBatteryWarning) {
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                 std::string(I18N.get(StrId::STR_BACKGROUND_SERVER_WARNING_TITLE)),
                                                 std::string(I18N.get(StrId::STR_BACKGROUND_SERVER_WARNING_BODY))),
          [this, setting, newValue, applyEnumValue, persistSettings](const ActivityResult& result) {
            if (!result.isCancelled) {
              applyEnumValue(setting, newValue);
              persistSettings();
            }
            requestUpdate();
          });
      return;
    }

    applyEnumValue(setting, newValue);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::STRING) {
    char* const stringPtr = setting.stringPtr;
    const size_t stringMaxLen = setting.stringMaxLen;
    auto stringSetter = setting.stringSetter;
    std::string currentValue;
    if (setting.stringGetter) {
      currentValue = setting.stringGetter();
    } else if (stringPtr) {
      currentValue = stringPtr;
    }
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(I18N.get(setting.nameId)),
                                                currentValue, stringMaxLen > 0 ? stringMaxLen - 1 : 64, false),
        [this, stringPtr, stringMaxLen, stringSetter](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            if (stringSetter) {
              stringSetter(kb.text);
            } else if (stringPtr && stringMaxLen > 0) {
              strncpy(stringPtr, kb.text.c_str(), stringMaxLen - 1);
              stringPtr[stringMaxLen - 1] = '\0';
              SETTINGS.validateAndClamp();
            }
            if (!SETTINGS.saveToFile()) {
              LOG_WRN("SETTINGS", "Failed to persist string setting");
            }
          }
        });
    return;
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::PokemonParty:
        activityManager.goToRecentBooks();
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::FactoryReset:
        startActivityForResult(
            std::make_unique<FactoryResetActivity>(renderer, mappedInput, [] { activityManager.popActivity(); }),
            resultHandler);
        break;
      case SettingAction::SwitchToTrmnl: {
        const esp_partition_t* next_partition = esp_ota_get_next_update_partition(NULL);
        if (next_partition != nullptr) {
          LOG_INF("SYSTEM", "Switching to next partition: %s", next_partition->label);
          esp_ota_set_boot_partition(next_partition);
          esp_restart();
        }
      } break;
      case SettingAction::ValidateSleepImages:
        startActivityForResult(
            std::make_unique<ValidateSleepImagesActivity>(renderer, mappedInput, [] { activityManager.popActivity(); }),
            resultHandler);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  persistSettings();
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP,
          StrId::STR_SLEEP_TIMER_STEP_HINT, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES,
          1, 5, StrId::STR_SLEEP_TIMER_VALUE_FORMAT),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedSettingIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM) {
          uint8_t value = 0;
          bool hasValue = false;
          if (setting.valuePtr != nullptr) {
            value = SETTINGS.*(setting.valuePtr);
            hasValue = true;
          } else if (setting.valueGetter) {
            value = setting.valueGetter();
            hasValue = true;
          }

          if (hasValue) {
            if (!setting.enumStringValues.empty()) {
              const size_t valueIndex = std::min(static_cast<size_t>(value), setting.enumStringValues.size() - 1);
              valueText = setting.enumStringValues[valueIndex];
            } else if (setting.dynamicValuesGetter) {
              const auto dynamicValues = setting.dynamicValuesGetter();
              if (!dynamicValues.empty()) {
                const size_t valueIndex = std::min(static_cast<size_t>(value), dynamicValues.size() - 1);
                valueText = dynamicValues[valueIndex];
              }
            } else if (!setting.enumValues.empty()) {
              const size_t valueIndex = std::min(static_cast<size_t>(value), setting.enumValues.size() - 1);
              valueText = I18N.get(setting.enumValues[valueIndex]);
            }
          }
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
            char valueBuffer[32];
            snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                     static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
            valueText = valueBuffer;
          } else {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr));
          }
        } else if (setting.type == SettingType::STRING) {
          if (setting.stringGetter) {
            valueText = setting.stringGetter();
          } else if (setting.stringPtr) {
            valueText = setting.stringPtr;
          }
        }
        return valueText;
      },
      true);

  // Draw help text
  const bool isSleepSetting = selectedSettingIndex > 0 &&
                               (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP;
  const auto confirmLabel = (selectedSettingIndex == 0)
                                ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
                                : (isSleepSetting ? tr(STR_SELECT) : tr(STR_TOGGLE));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
