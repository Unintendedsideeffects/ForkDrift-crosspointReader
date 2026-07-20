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
struct OpdsCredentials {
  std::string username;
  std::string password;
};

// Derives KOReader (KOSync) progress-sync settings from an existing OPDS server entry
// and applies them to the KOReader credential store:
//   serverUrl   = <origin of server.url> + "/api/koreader"  (Booklore/Grimoire KOSync path)
//   username    = server.username
//   password    = server.password
//   matchMethod = BINARY (Booklore matches OPDS-downloaded books by partial-MD5)
// Persists via FeatureModules::saveKoreaderSettings().
// Returns the derived sync URL, or "" if server.url has no extractable origin.
std::string applyOpdsServerToKoreaderSync(const OpdsServer& server);

#if CROSSPOINT_HOST_BUILD
namespace test_hooks {
extern std::string (*getKoreaderUsername)();
extern std::string (*getKoreaderPassword)();
extern std::string (*getKoreaderServerUrl)();
}  // namespace test_hooks
#endif

// Returns the credentials to use for an OPDS fetch: the server's own if set,
// otherwise the KOReader-sync credentials when they target the same host.
// (The stores are otherwise disjoint; the settings UI implies they carry over.)
OpdsCredentials effectiveOpdsCredentials(const OpdsServer& server);

}  // namespace core
