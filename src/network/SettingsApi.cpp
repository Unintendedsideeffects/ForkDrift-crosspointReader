#include "network/SettingsApi.h"

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "SettingsList.h"
#include "core/features/FeatureModules.h"

namespace {

bool isPasswordField(const char* key) {
  return key != nullptr && (strstr(key, "password") != nullptr || strstr(key, "Password") != nullptr);
}

void appendSettingJson(String& json, const SettingInfo& s) {
  JsonDocument doc;
  char output[640];
  constexpr size_t outputSize = sizeof(output);

  doc["key"] = s.key;
  doc["name"] = I18N.get(s.nameId);
  doc["category"] = I18N.get(s.category);

  switch (s.type) {
    case SettingType::TOGGLE: {
      doc["type"] = "toggle";
      if (s.valuePtr) {
        doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
      }
      break;
    }
    case SettingType::ENUM: {
      doc["type"] = "enum";
      if (s.valuePtr) {
        doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
      } else if (s.valueGetter) {
        doc["value"] = static_cast<int>(s.valueGetter());
      }
      JsonArray options = doc["options"].to<JsonArray>();
      if (s.dynamicValuesGetter) {
        for (const auto& opt : s.dynamicValuesGetter()) {
          options.add(opt.c_str());
        }
      } else {
        for (const auto& opt : s.enumValues) {
          options.add(I18N.get(opt));
        }
      }
      break;
    }
    case SettingType::VALUE: {
      doc["type"] = "value";
      if (s.valuePtr) {
        doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
      }
      doc["min"] = s.valueRange.min;
      doc["max"] = s.valueRange.max;
      doc["step"] = s.valueRange.step;
      break;
    }
    case SettingType::STRING: {
      doc["type"] = "string";
      if (isPasswordField(s.key)) {
        doc["value"] = "";
        if (s.stringGetter) {
          doc["hasValue"] = !s.stringGetter().empty();
        } else if (s.stringPtr) {
          doc["hasValue"] = s.stringPtr[0] != '\0';
        } else {
          doc["hasValue"] = false;
        }
      } else if (s.stringGetter) {
        doc["value"] = s.stringGetter();
      } else if (s.stringMaxLen > 0) {
        doc["value"] = reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset;
      }
      break;
    }
    default:
      return;
  }

  if (s.visibleWhen.key) {
    JsonObject vis = doc["visibleWhen"].to<JsonObject>();
    vis["key"] = s.visibleWhen.key;
    vis["eq"] = static_cast<int>(s.visibleWhen.eq);
  }

  const size_t written = serializeJson(doc, output, outputSize);
  if (written >= outputSize) {
    return;
  }

  if (json.length() > 1) {
    json += ",";
  }
  json += output;
}

}  // namespace

namespace network {

String buildSettingsListJson() {
  const auto settings = getSettingsList();
  String json = "[";

  for (const auto& s : settings) {
    if (!s.key) {
      continue;
    }
    appendSettingJson(json, s);
  }

  json += "]";
  return json;
}

SettingsApplyResult applySettingsJson(const String& body) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return {400, "text/plain", String("Invalid JSON: ") + err.c_str(), 0};
  }

  const auto settings = getSettingsList();
  int applied = 0;
  bool updatedKoreaderSettings = false;

  for (const auto& s : settings) {
    if (!s.key || !doc[s.key].is<JsonVariant>()) {
      continue;
    }

    switch (s.type) {
      case SettingType::TOGGLE: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        updatedKoreaderSettings = updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const size_t optionCount = s.dynamicValuesGetter ? s.dynamicValuesGetter().size() : s.enumValues.size();
        if (val >= 0 && val < static_cast<int>(optionCount)) {
          if (s.valuePtr) {
            if (s.valuePtr == &CrossPointSettings::frontButtonLayout) {
              SETTINGS.applyFrontButtonLayoutPreset(static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(val));
            } else {
              SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
            }
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
          updatedKoreaderSettings = updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
          updatedKoreaderSettings = updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        updatedKoreaderSettings = updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
        break;
      }
      default:
        break;
    }
  }

  core::FeatureModules::onWebSettingsApplied();
  if (updatedKoreaderSettings) {
    core::FeatureModules::saveKoreaderSettings();
  }

  SETTINGS.enforceButtonLayoutConstraints();
  if (!SETTINGS.saveToFile()) {
    return {500, "text/plain", "Failed to persist settings", applied};
  }

  return {200, "text/plain", String("Applied ") + String(applied) + " setting(s)", applied};
}

}  // namespace network
