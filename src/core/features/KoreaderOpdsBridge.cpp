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

#if CROSSPOINT_HOST_BUILD
namespace test_hooks {
std::string (*getKoreaderUsername)() = nullptr;
std::string (*getKoreaderPassword)() = nullptr;
std::string (*getKoreaderServerUrl)() = nullptr;
}  // namespace test_hooks
#endif

OpdsCredentials effectiveOpdsCredentials(const OpdsServer& server) {
  if (!server.username.empty()) {
    return {server.username, server.password};
  }

#if CROSSPOINT_HOST_BUILD
  std::string koUsername =
      test_hooks::getKoreaderUsername ? test_hooks::getKoreaderUsername() : FeatureModules::getKoreaderUsername();
#else
  std::string koUsername = FeatureModules::getKoreaderUsername();
#endif

  if (!koUsername.empty()) {
    std::string serverHost = UrlUtils::extractHost(server.url);
#if CROSSPOINT_HOST_BUILD
    std::string koServerUrl =
        test_hooks::getKoreaderServerUrl ? test_hooks::getKoreaderServerUrl() : FeatureModules::getKoreaderServerUrl();
#else
    std::string koServerUrl = FeatureModules::getKoreaderServerUrl();
#endif
    std::string koServerHost = UrlUtils::extractHost(koServerUrl);
    if (!serverHost.empty() && serverHost == koServerHost) {
#if CROSSPOINT_HOST_BUILD
      std::string koPassword =
          test_hooks::getKoreaderPassword ? test_hooks::getKoreaderPassword() : FeatureModules::getKoreaderPassword();
#else
      std::string koPassword = FeatureModules::getKoreaderPassword();
#endif
      return {koUsername, koPassword};
    }
  }

  return {"", ""};
}

}  // namespace core
