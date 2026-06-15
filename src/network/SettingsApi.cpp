#include "network/SettingsApi.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "SpiBusMutex.h"
#include "core/features/FeatureModules.h"
#include "network/SettingsApply.h"

namespace {

bool isPasswordField(const char* key) {
  return key != nullptr && (strstr(key, "password") != nullptr || strstr(key, "Password") != nullptr);
}

bool appendSettingJson(String& json, const SettingInfo& s) {
  JsonDocument doc;
  char output[768];
  constexpr size_t outputSize = sizeof(output);

  doc["key"] = s.key;
  doc["name"] = I18N.get(s.nameId);
  doc["category"] = I18N.get(s.category);

  switch (s.type) {
    case SettingType::TOGGLE: {
      doc["type"] = "toggle";
      doc["value"] = static_cast<int>(s.persistedValue());
      break;
    }
    case SettingType::ENUM: {
      doc["type"] = "enum";
      doc["value"] = static_cast<int>(s.persistedValue());
      JsonArray options = doc["options"].to<JsonArray>();
      JsonArray optionValues = doc["optionValues"].to<JsonArray>();
      if (s.dynamicValuesGetter) {
        for (const auto& opt : s.dynamicValuesGetter()) {
          options.add(opt.c_str());
        }
      } else {
        for (const auto& opt : s.enumValues) {
          options.add(I18N.get(opt));
        }
      }
      for (size_t i = 0; i < options.size(); ++i) {
        optionValues.add(i < s.enumPersistedValues.size() ? s.enumPersistedValues[i] : static_cast<uint8_t>(i));
      }
      break;
    }
    case SettingType::VALUE: {
      doc["type"] = "value";
      doc["value"] = static_cast<int>(s.persistedValue());
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
      return true;
  }

  if (s.visibleWhen.key) {
    JsonObject vis = doc["visibleWhen"].to<JsonObject>();
    vis["key"] = s.visibleWhen.key;
    if (!s.visibleWhen.eqAnyOf.empty()) {
      JsonArray values = vis["eqAnyOf"].to<JsonArray>();
      for (const uint8_t value : s.visibleWhen.eqAnyOf) {
        values.add(value);
      }
    } else if (s.visibleWhen.notEqual) {
      vis["ne"] = static_cast<int>(s.visibleWhen.eq);
    } else {
      vis["eq"] = static_cast<int>(s.visibleWhen.eq);
    }
  }

  const size_t requiredSize = measureJson(doc);
  if (requiredSize >= outputSize) {
    LOG_ERR("WEB", "Dropping oversized setting key=%s required=%u bytes", s.key ? s.key : "(null)",
            static_cast<unsigned>(requiredSize + 1));
    return false;
  }
  serializeJson(doc, output, outputSize);

  if (json.length() > 1) {
    json += ",";
  }
  json += output;
  return true;
}

}  // namespace

namespace network {

String buildSettingsListJson() {
  std::vector<SettingInfo> settings;
  {
    SpiBusMutex::Guard guard;
    sdFontSystem.refreshIfDirty();
    settings = getSettingsList(&sdFontSystem.registry());
  }

  String json = "[";
  size_t droppedCount = 0;

  for (const auto& s : settings) {
    if (!s.key) {
      continue;
    }
    if (!appendSettingJson(json, s)) {
      droppedCount++;
    }
  }

  json += "]";
  if (droppedCount > 0) {
    LOG_WRN("WEB", "Dropped %u oversized setting entries", static_cast<unsigned>(droppedCount));
  }
  return json;
}

SettingsApplyResult applySettingsJson(const String& body) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return {400, "text/plain", String("Invalid JSON: ") + err.c_str(), 0};
  }

  const auto settings = getSettingsList();
  const SettingsApplyTally tally = applySettingsToList(doc, settings);
  const int applied = tally.appliedCount;
  const bool updatedKoreaderSettings = tally.updatedKoreaderSettings;

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
