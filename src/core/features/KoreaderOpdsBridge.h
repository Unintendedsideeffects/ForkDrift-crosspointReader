#pragma once

#include <string>

struct OpdsServer;  // defined in OpdsServerStore.h

namespace core {

// Derives KOReader (KOSync) progress-sync settings from an existing OPDS server entry
// and applies them to the KOReader credential store:
//   serverUrl   = <origin of server.url> + "/api/koreader"  (Booklore/Grimoire KOSync path)
//   username    = server.username
//   password    = server.password
//   matchMethod = BINARY (Booklore matches OPDS-downloaded books by partial-MD5)
// Persists via FeatureModules::saveKoreaderSettings().
// Returns the derived sync URL, or "" if server.url has no extractable origin.
std::string applyOpdsServerToKoreaderSync(const OpdsServer& server);

}  // namespace core
