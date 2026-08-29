#include "features/web_wallpaper/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>

#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/html/WallpaperPluginPageHtml.generated.h"
#include "network/server/WebUtils.h"

namespace features::web_wallpaper {
namespace {

#if ENABLE_WEB_WALLPAPER_PLUGIN
bool shouldRegisterWallpaperPluginRoute() { return core::FeatureCatalog::isEnabled("web_wallpaper_plugin"); }

void handleWallpaperPage(WebServer* server) {
  sendPrecompressedHtml(server, WallpaperPluginPageHtml, WallpaperPluginPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served wallpaper plugin page");
}

const core::WebRouteSpec kWallpaperRoutes[] = {
    {"/plugins/wallpaper", HTTP_GET, handleWallpaperPage, nullptr},
};
#endif

}  // namespace

void registerFeature() {
#if ENABLE_WEB_WALLPAPER_PLUGIN
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "wallpaper_plugin";
  webRouteEntry.shouldRegister = shouldRegisterWallpaperPluginRoute;
  webRouteEntry.routes = kWallpaperRoutes;
  webRouteEntry.routeCount = sizeof(kWallpaperRoutes) / sizeof(kWallpaperRoutes[0]);
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_wallpaper
