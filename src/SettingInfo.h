#pragma once
#include <I18n.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING, SECTION_HEADER };

enum class SettingAction {
  None,
  RemapFrontButtons,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  PokemonParty,
  Network,
  ClearCache,
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  FactoryReset,
  ValidateSleepImages,
  ResetSettings,
  ClearWifiNetworks,
  ClearLogs,
  ClearCrashes,
  SwitchToTrmnl,
  DownloadFonts,
  TerminusSetup,
};

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::vector<std::string> enumStringValues;  // runtime alternative to StrId enumValues (for SD card fonts etc.)
  SettingAction action = SettingAction::None;

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;                     // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;          // Category for web UI grouping
  bool obfuscated = false;                       // Save/load via base64 obfuscation (passwords)
  bool configuratorExport = false;               // Include in static configurator schema/export
  bool configuratorHidden = false;               // Export, but render as hidden input in configurator
  const char* configuratorFeatureKey = nullptr;  // Feature-grid key required to expose this setting

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  char* stringPtr = nullptr;
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::vector<std::string>()> dynamicValuesGetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;
  std::vector<uint8_t> enumPersistedValues;        // Optional persisted values per enum option
  std::vector<const char*> enumOptionFeatureKeys;  // Optional feature-grid key per enum option

  // Visibility condition: show this setting only when another setting has a specific value.
  // Both fields are lightweight (const char* + uint8_t) — no heap allocation.
  struct VisibleWhen {
    const char* key = nullptr;  // Key of the controlling setting (null = always visible)
    uint8_t eq = 0;             // Required value of the controlling setting
    bool notEqual = false;      // True if condition is negation (not equal to eq)
    std::vector<uint8_t> eqAnyOf;
  };
  VisibleWhen visibleWhen;

  SettingInfo& withVisibleWhen(const char* dependsOnKey, uint8_t requiredValue) {
    visibleWhen.key = dependsOnKey;
    visibleWhen.eq = requiredValue;
    visibleWhen.notEqual = false;
    visibleWhen.eqAnyOf.clear();
    return *this;
  }

  SettingInfo& withVisibleWhenNot(const char* dependsOnKey, uint8_t requiredValue) {
    visibleWhen.key = dependsOnKey;
    visibleWhen.eq = requiredValue;
    visibleWhen.notEqual = true;
    visibleWhen.eqAnyOf.clear();
    return *this;
  }

  SettingInfo& withVisibleWhenAnyOf(const char* dependsOnKey, std::vector<uint8_t> requiredValues) {
    visibleWhen.key = dependsOnKey;
    visibleWhen.eq = 0;
    visibleWhen.notEqual = false;
    visibleWhen.eqAnyOf = std::move(requiredValues);
    return *this;
  }

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  SettingInfo& withConfiguratorExport(const char* featureKey = nullptr, bool hidden = false) {
    configuratorExport = true;
    configuratorHidden = hidden;
    configuratorFeatureKey = featureKey;
    return *this;
  }

  SettingInfo& withEnumPersistedValues(std::vector<uint8_t> values) {
    enumPersistedValues = std::move(values);
    return *this;
  }

  SettingInfo& withEnumOptionFeatureKeys(std::vector<const char*> values) {
    enumOptionFeatureKeys = std::move(values);
    return *this;
  }

  // valueGetter/valueSetter speak option positions; valuePtr speaks persisted
  // values; everything outside SettingInfo must use these four accessors.
  uint8_t persistedValue() const {
    if (valuePtr != nullptr) {
      return SETTINGS.*valuePtr;
    }
    if (!valueGetter) {
      return 0;
    }

    const uint8_t position = valueGetter();
    if (!enumPersistedValues.empty() && position < enumPersistedValues.size()) {
      return enumPersistedValues[position];
    }
    return position;
  }

  void setPersistedValue(const uint8_t value) const {
    if (valueSetter) {
      if (!enumPersistedValues.empty()) {
        const auto it = std::find(enumPersistedValues.begin(), enumPersistedValues.end(), value);
        if (it != enumPersistedValues.end()) {
          valueSetter(static_cast<uint8_t>(std::distance(enumPersistedValues.begin(), it)));
          return;
        }
      }
      valueSetter(value);
    } else if (valuePtr != nullptr) {
      SETTINGS.*valuePtr = value;
    }
  }

  size_t optionPosition() const {
    if (valueGetter) {
      return valueGetter();
    }
    if (valuePtr == nullptr) {
      return 0;
    }

    const uint8_t value = SETTINGS.*valuePtr;
    if (!enumPersistedValues.empty()) {
      const auto it = std::find(enumPersistedValues.begin(), enumPersistedValues.end(), value);
      return it == enumPersistedValues.end() ? enumPersistedValues.size()
                                             : static_cast<size_t>(std::distance(enumPersistedValues.begin(), it));
    }
    return value;
  }

  void activateOption(const size_t position) const {
    if (valueSetter) {
      valueSetter(static_cast<uint8_t>(position));
    } else if (valuePtr != nullptr) {
      SETTINGS.*valuePtr =
          position < enumPersistedValues.size() ? enumPersistedValues[position] : static_cast<uint8_t>(position);
    }
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo SectionHeader(StrId nameId) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::SECTION_HEADER;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringPtr = ptr;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT,
                                 std::function<std::vector<std::string>()> dynamicValuesGetter = nullptr) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.dynamicValuesGetter = std::move(dynamicValuesGetter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};
