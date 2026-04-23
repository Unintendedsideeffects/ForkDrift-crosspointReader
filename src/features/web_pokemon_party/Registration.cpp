#include "features/web_pokemon_party/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>

#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/WebUtils.h"
#include "network/html/PokemonPartyPluginPageHtml.generated.h"

namespace features::web_pokemon_party {
namespace {

#if ENABLE_POKEMON_PARTY
bool shouldRegisterPokemonPartyPageRoute() { return core::FeatureCatalog::isEnabled("pokemon_party"); }

void mountPokemonPartyRoutes(WebServer* server) {
  server->on("/plugins/pokemon-party", HTTP_GET, [server] {
    sendPrecompressedHtml(server, PokemonPartyPluginPageHtml, PokemonPartyPluginPageHtmlCompressedSize);
    LOG_DBG("WEB", "Served pokemon party plugin page");
  });
}
#endif

}  // namespace

void registerFeature() {
#if ENABLE_POKEMON_PARTY
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "pokemon_party_page";
  webRouteEntry.shouldRegister = shouldRegisterPokemonPartyPageRoute;
  webRouteEntry.mountRoutes = mountPokemonPartyRoutes;
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_pokemon_party
