#pragma once

#include <ArduinoJson.h>

#include <vector>

#include "SettingInfo.h"

namespace network {

struct SettingsApplyTally {
  int appliedCount = 0;
  bool updatedKoreaderSettings = false;
};

// Applies the values in `doc` to the matching entries in `settings` (matched by
// SettingInfo::key), validating each per its type (TOGGLE/ENUM/VALUE/STRING) and
// writing valid values through SettingInfo::setPersistedValue.
//
// This is the pure value-application half of the settings POST path: it does NOT
// build the settings list, touch the SD font system, or persist anything — the
// caller supplies the list (e.g. from getSettingsList()) and handles persistence.
// Splitting it out this way is what lets the apply/validation logic be host-tested
// without dragging in the SD font registry that getSettingsList() depends on.
void applySettingFromDoc(const SettingInfo& setting, JsonDocument& doc, SettingsApplyTally& tally);
SettingsApplyTally applySettingsToList(JsonDocument& doc, const std::vector<SettingInfo>& settings);

}  // namespace network
