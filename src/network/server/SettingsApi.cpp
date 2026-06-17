#include "network/server/SettingsApi.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "SpiBusMutex.h"
#include "core/features/FeatureModules.h"
#include "network/server/SettingsApply.h"

namespace {

bool isPasswordField(const char* key) {
  return key != nullptr && (strstr(key, "password") != nullptr || strstr(key, "Password") != nullptr);
}

// Serializes one setting as a JSON object into `output` (null-terminated).
// Returns the number of bytes written (excluding the null), or 0 if the setting
// produces no JSON (unhandled type) or does not fit (logged and dropped). The
// caller frames the surrounding array and commas — see streamSettingsListJson.
size_t serializeSettingJson(const SettingInfo& s, char* output, size_t outputSize) {
  JsonDocument doc;

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
      return 0;
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
    return 0;
  }
  return serializeJson(doc, output, outputSize);
}

// Per-setting buffer cap. measureJson() drops anything that would not fit rather
// than truncating, so this bounds peak stack use to one setting at a time.
constexpr size_t kSettingJsonCap = 768;

struct StreamState {
  network::SettingChunkSink sink;
  void* sinkCtx;
  bool first;
  uint16_t streamed;  // entries serialized and sent
  uint16_t dropped;   // keyed entries skipped because their JSON exceeded the buffer
};

// SettingSink for forEachSetting: serialize one setting and push it to the client
// as a chunk. Holds no heap state between calls — peak memory is one SettingInfo
// (from the generator) + one JsonDocument + the stack buffer below.
void streamSettingToSink(void* vstate, SettingInfo&& s) {
  auto* state = static_cast<StreamState*>(vstate);
  if (!s.key) {
    return;  // ACTION / SECTION_HEADER entries carry no key and emit no JSON.
  }
  char buf[kSettingJsonCap + 1];  // +1 leaves room for a leading comma.
  size_t offset = 0;
  if (!state->first) {
    buf[0] = ',';
    offset = 1;
  }
  const size_t written = serializeSettingJson(s, buf + offset, kSettingJsonCap);
  if (written == 0) {
    // Oversized: serializeSettingJson already logged the offending key. No comma
    // emitted and `first` unchanged, so the array stays well-formed.
    state->dropped++;
    return;
  }
  state->first = false;
  state->streamed++;
  state->sink(state->sinkCtx, buf);
}

}  // namespace

namespace network {

void streamSettingsListJson(SettingChunkSink sink, void* ctx) {
  // All SD / font-registry access is confined to this guarded prelude. The font
  // family setting captures copies of the family names (see buildFontFamilySetting),
  // so once built it needs no further SD access. That lets the stream below run
  // lock-free — we never hold the SPI bus across a chunked network write.
  bool hasSleepImages = false;
  bool hasPokedexImages = false;
  SettingInfo fontFamily;
  {
    SpiBusMutex::Guard guard;
    sdFontSystem.refreshIfDirty();
    hasSleepImages = dirHasAnyImage("/sleep");
    hasPokedexImages = dirHasAnyImage("/sleep/pokedex");
    fontFamily = buildFontFamilySetting(&sdFontSystem.registry());
  }

  StreamState state{sink, ctx, true, 0, 0};
  sink(ctx, "[");
  forEachSetting(&streamSettingToSink, &state, hasSleepImages, hasPokedexImages, std::move(fontFamily));
  sink(ctx, "]");

  if (state.dropped > 0) {
    LOG_WRN("WEB", "Settings stream dropped %u oversized entries", static_cast<unsigned>(state.dropped));
  }
  LOG_DBG("WEB", "Settings stream complete: %u sent, %u dropped", static_cast<unsigned>(state.streamed),
          static_cast<unsigned>(state.dropped));
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
