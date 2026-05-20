#include <ArduinoJson.h>

#include <map>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "SettingsList.h"

namespace {

const SettingInfo* findByKey(const std::vector<SettingInfo>& settings, const char* key) {
  if (key == nullptr) return nullptr;
  for (const auto& setting : settings) {
    if (setting.key != nullptr && std::string(setting.key) == key) {
      return &setting;
    }
  }
  return nullptr;
}

uint8_t mapVisibleWhenValue(const std::vector<SettingInfo>& settings, const SettingInfo::VisibleWhen& visibleWhen) {
  const SettingInfo* controller = findByKey(settings, visibleWhen.key);
  if (controller == nullptr || controller->enumPersistedValues.empty()) {
    return visibleWhen.eq;
  }
  if (visibleWhen.eq >= controller->enumPersistedValues.size()) {
    return visibleWhen.eq;
  }
  return controller->enumPersistedValues[visibleWhen.eq];
}

const char* settingTypeName(const SettingType type) {
  switch (type) {
    case SettingType::TOGGLE:
      return "toggle";
    case SettingType::ENUM:
      return "enum";
    case SettingType::VALUE:
      return "value";
    case SettingType::STRING:
      return "string";
    default:
      return "unknown";
  }
}

std::vector<std::string> buildOptionLabels(const SettingInfo& setting) {
  if (setting.dynamicValuesGetter) {
    return setting.dynamicValuesGetter();
  }
  if (!setting.enumStringValues.empty()) {
    return setting.enumStringValues;
  }

  std::vector<std::string> labels;
  labels.reserve(setting.enumValues.size());
  for (const auto value : setting.enumValues) {
    labels.emplace_back(I18N.get(value));
  }
  return labels;
}

int getNumericDefaultValue(const SettingInfo& setting) {
  if (setting.valuePtr != nullptr) {
    return static_cast<int>(SETTINGS.*(setting.valuePtr));
  }
  if (setting.valueGetter) {
    return static_cast<int>(setting.valueGetter());
  }
  return 0;
}

void appendEmitRule(JsonArray emits, const char* key, const std::vector<uint8_t>& values) {
  JsonObject emit = emits.add<JsonObject>();
  emit["key"] = key;
  JsonArray outValues = emit["values"].to<JsonArray>();
  for (const auto value : values) {
    outValues.add(value);
  }
}

void appendSchemaSetting(JsonArray out, const std::vector<SettingInfo>& settings, const SettingInfo& setting) {
  if (!setting.configuratorExport || setting.key == nullptr) {
    return;
  }

  JsonObject item = out.add<JsonObject>();
  item["key"] = setting.key;
  item["label"] = I18N.get(setting.nameId);
  item["category"] = I18N.get(setting.category);
  item["type"] = settingTypeName(setting.type);
  if (setting.configuratorHidden) {
    item["hidden"] = true;
  }
  if (setting.configuratorFeatureKey != nullptr) {
    item["featureKey"] = setting.configuratorFeatureKey;
  }

  switch (setting.type) {
    case SettingType::TOGGLE:
    case SettingType::VALUE:
      item["default"] = getNumericDefaultValue(setting);
      break;
    case SettingType::ENUM: {
      const int defaultValue = getNumericDefaultValue(setting);
      item["default"] = defaultValue;
      std::vector<std::string> labels = buildOptionLabels(setting);
      JsonArray options = item["options"].to<JsonArray>();
      for (size_t i = 0; i < labels.size(); ++i) {
        JsonObject option = options.add<JsonObject>();
        option["label"] = labels[i];
        const uint8_t value =
            i < setting.enumPersistedValues.size() ? setting.enumPersistedValues[i] : static_cast<uint8_t>(i);
        option["value"] = value;
        if (i < setting.enumOptionFeatureKeys.size() && setting.enumOptionFeatureKeys[i] != nullptr) {
          option["featureKey"] = setting.enumOptionFeatureKeys[i];
        }
      }
      if (std::string(setting.key) == "backgroundServerMode") {
        JsonArray emits = item["emits"].to<JsonArray>();
        appendEmitRule(emits, "backgroundServerOnCharge", {0, 1, 1});
        appendEmitRule(emits, "wifiAutoConnect", {0, 0, 1});
      }
      break;
    }
    case SettingType::STRING: {
      if (setting.stringGetter) {
        item["default"] = setting.stringGetter();
      } else if (setting.stringPtr != nullptr) {
        item["default"] = setting.stringPtr;
      } else {
        item["default"] = "";
      }
      item["maxLength"] =
          std::string(setting.key) == "deviceName" ? 24 : static_cast<int>(setting.stringMaxLen);
      break;
    }
    default:
      out.remove(out.size() - 1);
      return;
  }

  if (setting.type == SettingType::VALUE) {
    item["min"] = setting.valueRange.min;
    item["max"] = setting.valueRange.max;
    item["step"] = setting.valueRange.step;
  }

  if (setting.visibleWhen.key != nullptr) {
    JsonObject visibleWhen = item["visibleWhen"].to<JsonObject>();
    visibleWhen["key"] = setting.visibleWhen.key;
    visibleWhen["eq"] = mapVisibleWhenValue(settings, setting.visibleWhen);
  }
}

void appendSleepPinnedPath(JsonArray out) {
  JsonObject item = out.add<JsonObject>();
  item["key"] = "sleepPinnedPath";
  item["label"] = "Pinned Sleep Image Path";
  item["category"] = I18N.get(StrId::STR_CAT_DISPLAY);
  item["type"] = "string";
  item["default"] = SETTINGS.sleepPinnedPath;
  item["maxLength"] = static_cast<int>(sizeof(SETTINGS.sleepPinnedPath));
  JsonObject visibleWhen = item["visibleWhen"].to<JsonObject>();
  visibleWhen["key"] = "sleepScreen";
  visibleWhen["eq"] = static_cast<int>(CrossPointSettings::CUSTOM);
}

}  // namespace

int main() {
  JsonDocument root;
  root["version"] = 1;
  JsonArray settingsOut = root["settings"].to<JsonArray>();

  const auto settings = getSettingsList();
  for (const auto& setting : settings) {
    appendSchemaSetting(settingsOut, settings, setting);
  }
  appendSleepPinnedPath(settingsOut);

  String output;
  serializeJson(root, output);
  printf("%s\n", output.c_str());
  return 0;
}
