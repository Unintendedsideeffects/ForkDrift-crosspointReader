#include "network/server/SettingsApply.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"

namespace network {

void applySettingFromDoc(const SettingInfo& s, JsonDocument& doc, SettingsApplyTally& tally) {
  if (!s.key || !doc[s.key].is<JsonVariant>()) {
    return;
  }

  switch (s.type) {
    case SettingType::TOGGLE: {
      const int val = doc[s.key].as<int>() ? 1 : 0;
      s.setPersistedValue(val);
      tally.appliedCount++;
      tally.updatedKoreaderSettings = tally.updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
      break;
    }
    case SettingType::ENUM: {
      const int val = doc[s.key].as<int>();
      const size_t optionCount = s.dynamicValuesGetter ? s.dynamicValuesGetter().size() : s.enumValues.size();
      const bool validValue =
          val >= 0 && val <= UINT8_MAX &&
          (s.enumPersistedValues.empty() ? val < static_cast<int>(optionCount)
                                         : std::find(s.enumPersistedValues.begin(), s.enumPersistedValues.end(),
                                                     static_cast<uint8_t>(val)) != s.enumPersistedValues.end());
      if (validValue) {
        s.setPersistedValue(static_cast<uint8_t>(val));
        if (s.key != nullptr && std::strcmp(s.key, "frontButtonLayout") == 0) {
          SETTINGS.applyFrontButtonLayoutPreset(
              static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(s.persistedValue()));
        }
        tally.appliedCount++;
        tally.updatedKoreaderSettings = tally.updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
      }
      break;
    }
    case SettingType::VALUE: {
      const int val = doc[s.key].as<int>();
      if (val >= s.valueRange.min && val <= s.valueRange.max) {
        s.setPersistedValue(static_cast<uint8_t>(val));
        tally.appliedCount++;
        tally.updatedKoreaderSettings = tally.updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
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
      tally.appliedCount++;
      tally.updatedKoreaderSettings = tally.updatedKoreaderSettings || (s.category == StrId::STR_KOREADER_SYNC);
      break;
    }
    default:
      break;
  }
}

SettingsApplyTally applySettingsToList(JsonDocument& doc, const std::vector<SettingInfo>& settings) {
  SettingsApplyTally tally;
  for (const auto& s : settings) {
    applySettingFromDoc(s, doc, tally);
  }
  return tally;
}

}  // namespace network
