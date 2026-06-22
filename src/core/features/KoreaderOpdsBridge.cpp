#include "core/features/KoreaderOpdsBridge.h"

#include <cstdint>

#include "OpdsServerStore.h"
#include "core/features/FeatureModules.h"
#include "util/UrlUtils.h"

namespace core {

std::string applyOpdsServerToKoreaderSync(const OpdsServer& server) {
  const std::string origin = UrlUtils::extractHost(server.url);
  if (origin.empty()) {
    return "";
  }
  const std::string syncUrl = origin + "/api/koreader";

  // BINARY == 1 (DocumentMatchMethod in lib/KOReaderSync/KOReaderCredentialStore.h).
  constexpr uint8_t kMatchMethodBinary = 1;

  FeatureModules::setKoreaderServerUrl(syncUrl, false);
  FeatureModules::setKoreaderUsername(server.username, false);
  FeatureModules::setKoreaderPassword(server.password, false);
  FeatureModules::setKoreaderMatchMethod(kMatchMethodBinary, false);
  FeatureModules::saveKoreaderSettings();

  return syncUrl;
}

}  // namespace core
