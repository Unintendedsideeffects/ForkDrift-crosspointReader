#pragma once

#include <ArduinoJson.h>

class CrossPointSettings;

// Pure settings <-> JsonDocument conversion, with no file I/O and no obfuscation,
// so both the firmware (src/JsonSettingsIO.cpp) and the host-test stub
// (test/mock/JsonSettingsIO.cpp) can share exactly one implementation. Before this
// existed the two kept hand-maintained copies that had already drifted apart, and
// the host suite was only ever testing the copy. Keep this file free of HalStorage,
// ObfuscationUtils, and anything else unavailable on the host.
namespace settings_serializer {

// Populates `doc` from `s`. Does not serialize or write anything.
void toDoc(const CrossPointSettings& s, JsonDocument& doc);

// Applies `doc` to `s`, clamping hostile values to defaults. Sets `*needsResave`
// when a legacy value was migrated and the file should be rewritten. Returns false
// only on a structurally unusable document.
bool fromDoc(CrossPointSettings& s, const JsonDocument& doc, bool* needsResave);

}  // namespace settings_serializer
