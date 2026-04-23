#include "features/web_pokemon_wallpaper/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>

#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/WebUtils.h"
#include "network/html/PokemonWallpaperPluginPageHtml.generated.h"

namespace features::web_pokemon_wallpaper {
namespace {

#if ENABLE_POKEMON_WALLPAPER_PLUGIN
bool shouldRegisterPokemonWallpaperPluginRoute() { return core::FeatureCatalog::isEnabled("pokemon_wallpaper_plugin"); }

void mountPokemonWallpaperRoutes(WebServer* server) {
  server->on("/plugins/pokemon-wallpaper", HTTP_GET, [server] {
    sendPrecompressedHtml(server, PokemonWallpaperPluginPageHtml, PokemonWallpaperPluginPageHtmlCompressedSize);
    LOG_DBG("WEB", "Served pokemon wallpaper plugin page");
  });
}
#endif

}  // namespace

void registerFeature() {
#if ENABLE_POKEMON_WALLPAPER_PLUGIN
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "pokemon_wallpaper_plugin";
  webRouteEntry.shouldRegister = shouldRegisterPokemonWallpaperPluginRoute;
  webRouteEntry.mountRoutes = mountPokemonWallpaperRoutes;
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_pokemon_wallpaper
