#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FactoryResetActivity.h"
#include "FontDownloadActivity.h"
#include "FontSelectionActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "ResetSettingsActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "StatusBarSettingsActivity.h"
#include "ValidateSleepImagesActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/SettingsTopics.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/ListPickerActivity.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "network/BackgroundWifiService.h"
#include "util/MaintenanceUtils.h"
#include "util/NetworkNames.h"

namespace {
constexpr char kBackgroundServerModeKey[] = "backgroundServerMode";
constexpr uint32_t kMinHeapForSettingsRebuild = 48000;

size_t enumOptionCount(const SettingInfo& setting) {
  if (!setting.enumStringValues.empty()) {
    return setting.enumStringValues.size();
  }
  if (setting.dynamicValuesGetter) {
    return setting.dynamicValuesGetter().size();
  }
  return setting.enumValues.size();
}

uint8_t cycleEnumOptionIndex(const SettingInfo& setting) {
  const size_t optionCount = enumOptionCount(setting);
  if (optionCount == 0 || optionCount > 255) {
    return 0;
  }

  const size_t currentIndex = setting.valueGetter
                                  ? setting.valueGetter()
                                  : (setting.valuePtr ? static_cast<size_t>(SETTINGS.*(setting.valuePtr)) : 0);
  if (currentIndex >= optionCount) {
    return 0;
  }
  return static_cast<uint8_t>((currentIndex + 1) % optionCount);
}

std::string enumOptionLabel(const SettingInfo& setting, const uint8_t index) {
  if (!setting.enumStringValues.empty()) {
    return index < setting.enumStringValues.size() ? setting.enumStringValues[index] : std::string();
  }
  if (setting.dynamicValuesGetter) {
    const auto values = setting.dynamicValuesGetter();
    return index < values.size() ? values[index] : std::string();
  }
  if (index < setting.enumValues.size()) {
    return I18N.get(setting.enumValues[index]);
  }
  return std::string();
}

bool isSettingKeyReferencedInDependencies(const char* key, const std::vector<SettingInfo>& allSettings) {
  if (key == nullptr) return false;
  for (const auto& s : allSettings) {
    if (s.visibleWhen.key != nullptr && std::strcmp(s.visibleWhen.key, key) == 0) {
      return true;
    }
  }
  return false;
}

bool controlSettingVisible(const SettingInfo& setting, const std::vector<SettingInfo>& allSettings) {
  if (setting.key != nullptr && std::strcmp(setting.key, "timeZoneOffset") == 0) {
    return SETTINGS.timeMode != CrossPointSettings::TIME_MODE_MANUAL;
  }
  if (setting.visibleWhen.key == nullptr) {
    return true;
  }

  const auto it = std::find_if(allSettings.begin(), allSettings.end(), [&](const SettingInfo& s) {
    return s.key && std::strcmp(s.key, setting.visibleWhen.key) == 0;
  });
  if (it == allSettings.end()) {
    return true;
  }

  uint8_t value = 0;
  if (it->valueGetter) {
    value = it->valueGetter();
    // Dynamic enums report the option INDEX; visibleWhen targets the
    // persisted value (e.g. HAIKU_CLOCK_SLEEP=13 vs menu position 7), so
    // translate through the persisted-values table when one exists.
    if (!it->enumPersistedValues.empty()) {
      if (value < it->enumPersistedValues.size()) {
        value = it->enumPersistedValues[value];
      } else {
        return true;
      }
    }
  } else if (it->valuePtr) {
    value = SETTINGS.*(it->valuePtr);
  } else {
    return true;
  }

  if (setting.visibleWhen.notEqual) {
    return value != setting.visibleWhen.eq;
  } else {
    return value == setting.visibleWhen.eq;
  }
}

}  // namespace

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_TAB_READING,  StrId::STR_TAB_LOOKS,
                                                              StrId::STR_TAB_CONTROLS, StrId::STR_TAB_CONNECT,
                                                              StrId::STR_TAB_SYSTEM,   StrId::STR_TAB_ADVANCED};

void SettingsActivity::invalidateMasterSettingsCache() { cachedMasterSettings.clear(); }

void SettingsActivity::rebuildSettingsLists() {
  for (auto& list : settingsByCategory) {
    list.clear();
  }

  if (ESP.getFreeHeap() >= kMinHeapForSettingsRebuild) {
    const size_t priorFamilyCount = cachedMasterSettings.empty() ? 0 : sdFontSystem.registry().getFamilies().size();
    sdFontSystem.refreshIfDirty();
    if (!cachedMasterSettings.empty() && sdFontSystem.registry().getFamilies().size() != priorFamilyCount) {
      cachedMasterSettings.clear();
    }
  }

  if (cachedMasterSettings.empty()) {
    cachedMasterSettings = getSettingsList(&sdFontSystem.registry());
  }

  const auto& allSettings = cachedMasterSettings;

  // 1. Reading (Index 0)
  auto& readingSettings = settingsByCategory[0];
  for (auto& setting : allSettings) {
    if (setting.category == StrId::STR_CAT_READER) {
      readingSettings.push_back(setting);
    }
  }
  groupSettingsByTopic(readingSettings, settings_topics::kReader);
  if (!readingSettings.empty()) {
    const auto layoutHeaderIt = std::find_if(readingSettings.begin(), readingSettings.end(), [](const SettingInfo& s) {
      return s.type == SettingType::SECTION_HEADER && s.nameId == StrId::STR_SEC_LAYOUT;
    });
    const auto insertPos = layoutHeaderIt != readingSettings.end() ? layoutHeaderIt : readingSettings.end();
    readingSettings.insert(insertPos, SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  }
  readingSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  // 2. Looks (Index 1)
  auto& looksSettings = settingsByCategory[1];
  for (auto& setting : allSettings) {
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      if (controlSettingVisible(setting, allSettings)) {
        looksSettings.push_back(setting);
      }
    }
  }
  groupSettingsByTopic(looksSettings, settings_topics::kDisplay);

  // 3. Controls (Index 2)
  auto& controlsSettings = settingsByCategory[2];
  auto addControlSetting = [&](StrId nameId) {
    const auto it =
        std::find_if(allSettings.begin(), allSettings.end(), [nameId](const auto& s) { return s.nameId == nameId; });
    if (it != allSettings.end() && controlSettingVisible(*it, allSettings)) {
      controlsSettings.push_back(*it);
    }
  };
  auto addControlSettingByKey = [&](const char* key) {
    const auto it = std::find_if(allSettings.begin(), allSettings.end(), [key](const auto& setting) {
      return setting.key && std::strcmp(setting.key, key) == 0;
    });
    if (it != allSettings.end()) {
      if (controlSettingVisible(*it, allSettings)) {
        controlsSettings.push_back(*it);
      }
      return;
    }
    LOG_ERR("SET", "Missing control setting definition for key=%s", key);
  };
  controlsSettings.reserve(15);
  controlsSettings.push_back(SettingInfo::SectionHeader(StrId::STR_POWER_BUTTON));
  addControlSetting(StrId::STR_SHORT_PWR_BTN);
  addControlSetting(StrId::STR_LONG_PRESS_ACTION);
  controlsSettings.push_back(SettingInfo::SectionHeader(StrId::STR_FRONT_BUTTONS));
  controlsSettings.push_back(SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  addControlSettingByKey("frontButtonOrientationAware");
  addControlSetting(StrId::STR_LONG_PRESS_BEHAVIOR);
  addControlSetting(StrId::STR_LONG_PRESS_MENU_ACTION);
  controlsSettings.push_back(SettingInfo::SectionHeader(StrId::STR_SIDE_BUTTONS));
  addControlSetting(StrId::STR_SIDE_BTN_LAYOUT);
  addControlSettingByKey("sideButtonOrientationAware");
  addControlSetting(StrId::STR_SIDE_BTN_LONG_PRESS);

  // 4. Connect (Index 3)
  auto& connectSettings = settingsByCategory[3];
  auto addConnectSettingByKey = [&](const char* key) {
    const auto it = std::find_if(allSettings.begin(), allSettings.end(), [key](const auto& setting) {
      return setting.key && std::strcmp(setting.key, key) == 0;
    });
    if (it == allSettings.end()) {
      LOG_ERR("SET", "Missing connect setting definition for key=%s", key);
      return;
    }
    if (controlSettingVisible(*it, allSettings)) {
      connectSettings.push_back(*it);
    }
  };
  auto addConnectAction = [&](SettingAction action) {
    if (!core::FeatureModules::supportsSettingAction(action)) return;
    const auto it = std::find_if(allSettings.begin(), allSettings.end(), [&](const SettingInfo& setting) {
      return setting.type == SettingType::ACTION && setting.action == action;
    });
    if (it != allSettings.end()) {
      connectSettings.push_back(*it);
    }
  };
  auto addConnectActionDirect = [&](StrId nameId, SettingAction action) {
    if (!core::FeatureModules::supportsSettingAction(action)) return;
    connectSettings.push_back(SettingInfo::Action(nameId, action));
  };
  auto appendConnectTopic = [&](StrId header, const auto& addItems) {
    const size_t before = connectSettings.size();
    connectSettings.push_back(SettingInfo::SectionHeader(header));
    addItems();
    if (connectSettings.size() == before + 1) {
      connectSettings.pop_back();
    }
  };

  const size_t beforeConnect = connectSettings.size();
  connectSettings.push_back(SettingInfo::SectionHeader(StrId::STR_SEC_CONNECTIVITY));
  addConnectActionDirect(StrId::STR_WIFI_NETWORKS, SettingAction::Network);
  addConnectActionDirect(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync);
  addConnectActionDirect(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser);
  addConnectAction(SettingAction::TerminusSetup);
  addConnectAction(SettingAction::SwitchToTrmnl);
  if (connectSettings.size() == beforeConnect + 1) {
    connectSettings.pop_back();
  }

  appendConnectTopic(StrId::STR_SEC_FILE_SERVER, [&] {
    addConnectSettingByKey("usbMscPromptOnConnect");
    addConnectSettingByKey("backgroundServerMode");
  });
  appendConnectTopic(StrId::STR_SEC_ANKI_CONNECT, [&] {
    addConnectSettingByKey("ankiConnectUrl");
    addConnectSettingByKey("ankiConnectDeck");
  });

  // 5. System (Index 4)
  auto& systemSettings = settingsByCategory[4];
  auto addSystemSettingByKey = [&](const char* key) {
    const auto it = std::find_if(allSettings.begin(), allSettings.end(), [key](const auto& setting) {
      return setting.key && std::strcmp(setting.key, key) == 0;
    });
    if (it == allSettings.end()) {
      LOG_ERR("SET", "Missing system setting definition for key=%s", key);
      return;
    }
    if (controlSettingVisible(*it, allSettings)) {
      systemSettings.push_back(*it);
    }
  };
  auto addSystemActionDirect = [&](StrId nameId, SettingAction action) {
    if (!core::FeatureModules::supportsSettingAction(action)) return;
    systemSettings.push_back(SettingInfo::Action(nameId, action));
  };

  systemSettings.push_back(SettingInfo::SectionHeader(StrId::STR_SEC_GENERAL));
  for (const char* key : settings_topics::kGeneralSystemKeys) {
    addSystemSettingByKey(key);
  }

#if ENABLE_WIFI_CLOCK
  systemSettings.push_back(SettingInfo::SectionHeader(StrId::STR_CAT_TIME));
  addSystemSettingByKey("timeMode");
  addSystemSettingByKey("timeZoneOffset");
#endif

  addSystemActionDirect(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates);
  addSystemActionDirect(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate);
  addSystemActionDirect(StrId::STR_LANGUAGE, SettingAction::Language);

  systemSettings.push_back(SettingInfo::SectionHeader(StrId::STR_SEC_MAINTENANCE));
  addSystemActionDirect(StrId::STR_VALIDATE_SLEEP_IMAGES, SettingAction::ValidateSleepImages);
  addSystemActionDirect(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache);
  addSystemActionDirect(StrId::STR_CLEAR_LOGS, SettingAction::ClearLogs);

  // 6. Advanced (Index 5)
  auto& advancedSettings = settingsByCategory[5];
  auto addAdvancedSettingByKey = [&](const char* key) {
    const auto it = std::find_if(allSettings.begin(), allSettings.end(), [key](const auto& setting) {
      return setting.key && std::strcmp(setting.key, key) == 0;
    });
    if (it == allSettings.end()) {
      LOG_ERR("SET", "Missing advanced setting definition for key=%s", key);
      return;
    }
    if (controlSettingVisible(*it, allSettings)) {
      advancedSettings.push_back(*it);
    }
  };
  auto addAdvancedActionDirect = [&](StrId nameId, SettingAction action) {
    if (!core::FeatureModules::supportsSettingAction(action)) return;
    advancedSettings.push_back(SettingInfo::Action(nameId, action));
  };

  advancedSettings.push_back(SettingInfo::SectionHeader(StrId::STR_CAT_ADVANCED));
  addAdvancedSettingByKey("developerMode");
  addAdvancedSettingByKey("deviceName");

  advancedSettings.push_back(SettingInfo::SectionHeader(StrId::STR_SEC_MAINTENANCE));
  addAdvancedActionDirect(StrId::STR_RESET_SETTINGS, SettingAction::ResetSettings);
  addAdvancedActionDirect(StrId::STR_CLEAR_WIFI_NETWORKS, SettingAction::ClearWifiNetworks);
  addAdvancedActionDirect(StrId::STR_CLEAR_CRASHES, SettingAction::ClearCrashes);
  addAdvancedActionDirect(StrId::STR_FACTORY_RESET, SettingAction::FactoryReset);

  currentSettings = &settingsByCategory[selectedCategoryIndex];
  settingsCount = static_cast<int>(currentSettings->size());
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  if (BG_WIFI.isPendingOrRunning()) {
    BG_WIFI.stop(true);
  }

  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;

  invalidateMasterSettingsCache();
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
    while (selectedSettingIndex > 0 && selectedSettingIndex <= settingsCount &&
           (*currentSettings)[selectedSettingIndex - 1].type == SettingType::SECTION_HEADER) {
      selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    while (selectedSettingIndex > 0 && selectedSettingIndex <= settingsCount &&
           (*currentSettings)[selectedSettingIndex - 1].type == SettingType::SECTION_HEADER) {
      selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    }
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
    currentSettings = &settingsByCategory[selectedCategoryIndex];
    settingsCount = static_cast<int>(currentSettings->size());
    // Advance past any leading section headers
    while (selectedSettingIndex > 0 && selectedSettingIndex <= settingsCount &&
           (*currentSettings)[selectedSettingIndex - 1].type == SettingType::SECTION_HEADER) {
      selectedSettingIndex++;
    }
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

  currentSettings = &settingsByCategory[selectedCategoryIndex];
  settingsCount = static_cast<int>(currentSettings->size());

  requestUpdate();
}

#ifdef HOST_BUILD
void SettingsActivity::toggleCurrentSetting() {}
#else
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

#if ENABLE_WIFI_CLOCK
  // The timezone setting only exists when the WiFi clock is compiled in (the menu
  // entry is gated the same way at buildSettingsLists); timezoneOffsetOptions() is
  // likewise gated, so this handler must be too or lean profiles fail to link.
  if (setting.key != nullptr && std::strcmp(setting.key, "timeZoneOffset") == 0) {
    auto items = timezoneOffsetOptions();
    const int current = SETTINGS.timeZoneOffset;
    startActivityForResult(std::make_unique<ListPickerActivity>(renderer, mappedInput, StrId::STR_TIMEZONE_OFFSET,
                                                                std::move(items), current),
                           [this](const ActivityResult& r) {
                             if (!r.isCancelled && std::holds_alternative<ListPickerResult>(r.data)) {
                               const int idx = std::get<ListPickerResult>(r.data).selectedIndex;
                               if (idx >= 0 && idx <= 26) {
                                 SETTINGS.timeZoneOffset = static_cast<uint8_t>(idx);
                                 if (!SETTINGS.saveToFile()) LOG_WRN("SETTINGS", "Failed to persist timezone setting");
                               }
                             }
                             requestUpdate();
                           });
    return;
  }
#endif

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
    if (setting.key != nullptr && isSettingKeyReferencedInDependencies(setting.key, cachedMasterSettings)) {
      const int previousSelection = selectedSettingIndex;
      rebuildSettingsLists();
      selectedSettingIndex = std::min(previousSelection, settingsCount);
    }
  } else if (setting.type == SettingType::ENUM) {
    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                             [this](const ActivityResult&) {
                               core::FeatureModules::onFontFamilySettingChanged(SETTINGS.fontFamily);
                               if (!SETTINGS.saveToFile()) {
                                 LOG_ERR("SET", "Failed to save settings");
                               }
                               invalidateMasterSettingsCache();
                               rebuildSettingsLists();
                               requestUpdate();
                             });
      return;
    }

    const size_t optionCount = enumOptionCount(setting);
    if (optionCount == 0) {
      return;
    }
    const auto applyEnumValue = [this](const SettingInfo& targetSetting, const uint8_t value) {
      if (targetSetting.valueSetter) {
        targetSetting.valueSetter(value);
      } else if (targetSetting.valuePtr) {
        SETTINGS.*(targetSetting.valuePtr) = value;
      }

      if (targetSetting.key != nullptr && std::strcmp(targetSetting.key, "sleepScreen") == 0) {
        SETTINGS.sleepScreen = CrossPointSettings::normalizeSleepScreenMode(SETTINGS.sleepScreen);
      }

      if (targetSetting.valuePtr == &CrossPointSettings::frontButtonLayout) {
        SETTINGS.applyFrontButtonLayoutPreset(
            static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout));
      }

      if (targetSetting.valuePtr == &CrossPointSettings::fontFamily) {
        core::FeatureModules::onFontFamilySettingChanged(value);
      }
    };

    if (optionCount > 4) {
      std::vector<std::string> items;
      items.reserve(optionCount);
      for (size_t i = 0; i < optionCount; ++i) {
        items.push_back(enumOptionLabel(setting, i));
      }
      const uint8_t currentIndex = setting.valueGetter
                                       ? setting.valueGetter()
                                       : (setting.valuePtr ? SETTINGS.*(setting.valuePtr) : static_cast<uint8_t>(0));
      const uint8_t safeIndex = currentIndex < optionCount ? currentIndex : 0;

      startActivityForResult(
          std::make_unique<ListPickerActivity>(renderer, mappedInput, setting.nameId, std::move(items), safeIndex),
          [this, setting, applyEnumValue, persistSettings, optionCount](const ActivityResult& result) {
            if (!result.isCancelled && std::holds_alternative<ListPickerResult>(result.data)) {
              const int idx = std::get<ListPickerResult>(result.data).selectedIndex;
              if (idx >= 0 && idx < static_cast<int>(optionCount)) {
                const uint8_t newValue = static_cast<uint8_t>(idx);
                const uint8_t currentIndex =
                    setting.valueGetter ? setting.valueGetter()
                                        : (setting.valuePtr ? SETTINGS.*(setting.valuePtr) : static_cast<uint8_t>(0));
                const bool requiresBatteryWarning = setting.key != nullptr &&
                                                    strcmp(setting.key, kBackgroundServerModeKey) == 0 &&
                                                    currentIndex != CrossPointSettings::BACKGROUND_SERVER_ALWAYS &&
                                                    newValue == CrossPointSettings::BACKGROUND_SERVER_ALWAYS;
                if (requiresBatteryWarning) {
                  startActivityForResult(
                      std::make_unique<ConfirmationActivity>(
                          renderer, mappedInput, std::string(I18N.get(StrId::STR_BACKGROUND_SERVER_WARNING_TITLE)),
                          std::string(I18N.get(StrId::STR_BACKGROUND_SERVER_WARNING_BODY))),
                      [this, setting, newValue, applyEnumValue, persistSettings](const ActivityResult& res) {
                        if (!res.isCancelled) {
                          applyEnumValue(setting, newValue);
                          persistSettings();
                          if (setting.key != nullptr &&
                              isSettingKeyReferencedInDependencies(setting.key, cachedMasterSettings)) {
                            const int previousSelection = selectedSettingIndex;
                            rebuildSettingsLists();
                            selectedSettingIndex = std::min(previousSelection, settingsCount);
                          }
                        }
                        requestUpdate();
                      });
                } else {
                  applyEnumValue(setting, newValue);
                  persistSettings();
                  if (setting.key != nullptr &&
                      isSettingKeyReferencedInDependencies(setting.key, cachedMasterSettings)) {
                    const int previousSelection = selectedSettingIndex;
                    rebuildSettingsLists();
                    selectedSettingIndex = std::min(previousSelection, settingsCount);
                  }
                  requestUpdate();
                }
              }
            } else {
              requestUpdate();
            }
          });
      return;
    }

    const uint8_t newValue = cycleEnumOptionIndex(setting);
    const uint8_t currentIndex = setting.valueGetter
                                     ? setting.valueGetter()
                                     : (setting.valuePtr ? SETTINGS.*(setting.valuePtr) : static_cast<uint8_t>(0));
    const bool requiresBatteryWarning = setting.key != nullptr && strcmp(setting.key, kBackgroundServerModeKey) == 0 &&
                                        currentIndex != CrossPointSettings::BACKGROUND_SERVER_ALWAYS &&
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
    if (setting.key != nullptr && isSettingKeyReferencedInDependencies(setting.key, cachedMasterSettings)) {
      const int previousSelection = selectedSettingIndex;
      rebuildSettingsLists();
      selectedSettingIndex = std::min(previousSelection, settingsCount);
    }
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
                                 if (!SETTINGS.saveToFile()) {
                                   LOG_ERR("SET", "Failed to save settings");
                                 }
                                 invalidateMasterSettingsCache();
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
      case SettingAction::TerminusSetup: {
        char hostname[40];
        NetworkNames::getDeviceHostname(hostname, sizeof(hostname));
        char msg[120];
        snprintf(msg, sizeof(msg), "Configure Terminus at:\nhttp://%s.local/plugins/terminus", hostname);
        startActivityForResult(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::string(msg)),
                               resultHandler);
      } break;
      case SettingAction::ValidateSleepImages:
        startActivityForResult(
            std::make_unique<ValidateSleepImagesActivity>(renderer, mappedInput, [] { activityManager.popActivity(); }),
            resultHandler);
        break;
      case SettingAction::ResetSettings:
        startActivityForResult(std::make_unique<ResetSettingsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 invalidateMasterSettingsCache();
                                 rebuildSettingsLists();
                                 requestUpdate();
                               });
        break;
      case SettingAction::ClearWifiNetworks:
        startActivityForResult(std::make_unique<ConfirmationActivity>(
                                   renderer, mappedInput, std::string(I18N.get(StrId::STR_CLEAR_WIFI_NETWORKS)),
                                   std::string(I18N.get(StrId::STR_CLEAR_WIFI_WARNING))),
                               [this](const ActivityResult& result) {
                                 if (!result.isCancelled) {
                                   MaintenanceUtils::clearWifiNetworks();
                                 }
                                 requestUpdate();
                               });
        break;
      case SettingAction::ClearLogs:
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, std::string(I18N.get(StrId::STR_CLEAR_LOGS)),
                                                   std::string(I18N.get(StrId::STR_CLEAR_LOGS_WARNING))),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                MaintenanceUtils::clearLogs();
              }
              requestUpdate();
            });
        break;
      case SettingAction::ClearCrashes:
        startActivityForResult(std::make_unique<ConfirmationActivity>(
                                   renderer, mappedInput, std::string(I18N.get(StrId::STR_CLEAR_CRASHES)),
                                   std::string(I18N.get(StrId::STR_CLEAR_CRASHES_WARNING))),
                               [this](const ActivityResult& result) {
                                 if (!result.isCancelled) {
                                   MaintenanceUtils::clearCrashReports();
                                 }
                                 requestUpdate();
                               });
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
#endif

#ifdef HOST_BUILD
void SettingsActivity::openSleepTimeoutPicker() {}
#else
void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, StrId::STR_SLEEP_TIMER_STEP_HINT,
          SETTINGS.sleepTimeoutMinutes, CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES,
          CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5, StrId::STR_SLEEP_TIMER_VALUE_FORMAT),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          if (!SETTINGS.saveToFile()) {
            LOG_ERR("SET", "Failed to save settings");
          }
        }
        requestUpdate();
      });
}
#endif

void SettingsActivity::render(RenderLock&&) {
  if (APP_STATE.consumeTransparentSleepWakePaint()) {
    return;
  }

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
              const size_t valueIndex = (value < setting.enumStringValues.size()) ? value : 0;
              valueText = setting.enumStringValues[valueIndex];
            } else if (setting.dynamicValuesGetter) {
              const auto dynamicValues = setting.dynamicValuesGetter();
              if (!dynamicValues.empty()) {
                const size_t valueIndex = (value < dynamicValues.size()) ? value : 0;
                valueText = dynamicValues[valueIndex];
              }
            } else if (!setting.enumValues.empty()) {
              const size_t valueIndex = (value < setting.enumValues.size()) ? value : 0;
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
      true, [&settings](int i) { return settings[i].type == SettingType::SECTION_HEADER; });

  // Draw help text
  const bool isSleepSetting =
      selectedSettingIndex > 0 && (*currentSettings)[selectedSettingIndex - 1].nameId == StrId::STR_TIME_TO_SLEEP;
  const auto confirmLabel = (selectedSettingIndex == 0)
                                ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
                                : (isSleepSetting ? tr(STR_SELECT) : tr(STR_TOGGLE));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // List UIs stay on FAST_REFRESH — the full (inverting) refresh is reserved
  // for leaving the reader, where dense-text ghosting warrants the flash.
  // See ActivityManager::goHome.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
