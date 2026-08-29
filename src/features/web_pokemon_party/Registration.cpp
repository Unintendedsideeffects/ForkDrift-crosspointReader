#include "features/web_pokemon_party/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>

#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/html/PokemonPartyPluginPageHtml.generated.h"
#include "network/server/WebUtils.h"

namespace features::web_pokemon_party {
namespace {

#if ENABLE_POKEMON_PARTY
bool shouldRegisterPokemonPartyPageRoute() { return core::FeatureCatalog::isEnabled("pokemon_party"); }

void handlePokemonPartyPage(WebServer* server) {
  sendPrecompressedHtml(server, PokemonPartyPluginPageHtml, PokemonPartyPluginPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served pokemon party plugin page");
}

const core::WebRouteSpec kPokemonPartyPageRoutes[] = {
    {"/plugins/pokemon-party", HTTP_GET, handlePokemonPartyPage, nullptr},
};
#endif

}  // namespace

void registerFeature() {
#if ENABLE_POKEMON_PARTY
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "pokemon_party_page";
  webRouteEntry.shouldRegister = shouldRegisterPokemonPartyPageRoute;
  webRouteEntry.routes = kPokemonPartyPageRoutes;
  webRouteEntry.routeCount = sizeof(kPokemonPartyPageRoutes) / sizeof(kPokemonPartyPageRoutes[0]);
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_pokemon_party
