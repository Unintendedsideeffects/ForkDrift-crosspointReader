#include "features/web_pokemon_wallpaper/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>

#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/html/PokemonWallpaperPluginPageHtml.generated.h"
#include "network/server/WebUtils.h"

namespace features::web_pokemon_wallpaper {
namespace {

#if ENABLE_POKEMON_WALLPAPER_PLUGIN
bool shouldRegisterPokemonWallpaperPluginRoute() { return core::FeatureCatalog::isEnabled("pokemon_wallpaper_plugin"); }

void handlePokemonWallpaperPage(WebServer* server) {
  sendPrecompressedHtml(server, PokemonWallpaperPluginPageHtml, PokemonWallpaperPluginPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served pokemon wallpaper plugin page");
}

const core::WebRouteSpec kPokemonWallpaperRoutes[] = {
    {"/plugins/pokemon-wallpaper", HTTP_GET, handlePokemonWallpaperPage, nullptr},
};
#endif

}  // namespace

void registerFeature() {
#if ENABLE_POKEMON_WALLPAPER_PLUGIN
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "pokemon_wallpaper_plugin";
  webRouteEntry.shouldRegister = shouldRegisterPokemonWallpaperPluginRoute;
  webRouteEntry.routes = kPokemonWallpaperRoutes;
  webRouteEntry.routeCount = sizeof(kPokemonWallpaperRoutes) / sizeof(kPokemonWallpaperRoutes[0]);
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_pokemon_wallpaper
