#include "network/server/WebRouteTableHandler.h"

#include <FeatureFlags.h>

#include <cstring>

#include "core/registries/WebRouteRegistry.h"
#include "network/server/CrossPointWebServer.h"

WebRouteTableHandler::WebRouteTableHandler(CrossPointWebServer* owner) : owner(owner) {}

bool WebRouteTableHandler::coreMatches(const CoreRoute& route, const HTTPMethod method, const char* uri) {
  if (route.uri == nullptr || uri == nullptr || route.fn == nullptr) {
    return false;
  }
  if (route.method != HTTP_ANY && route.method != method) {
    return false;
  }
  return std::strcmp(route.uri, uri) == 0;
}

const WebRouteTableHandler::CoreRoute* WebRouteTableHandler::matchCore(const HTTPMethod method, const char* uri) {
  for (size_t i = 0; i < kCoreCount; ++i) {
    if (coreMatches(kCore[i], method, uri)) {
      return &kCore[i];
    }
  }
  return nullptr;
}

bool WebRouteTableHandler::canHandle(const HTTPMethod method, String uri) {
  const char* requestUri = uri.c_str();
  if (matchCore(method, requestUri) != nullptr) {
    return true;
  }
  return core::WebRouteRegistry::match(method, requestUri) != nullptr;
}

bool WebRouteTableHandler::canUpload(String uri) {
  const char* requestUri = uri.c_str();
  if (const CoreRoute* core = matchCore(HTTP_POST, requestUri)) {
    return core->ufn != nullptr;
  }
  const core::WebRouteSpec* plugin = core::WebRouteRegistry::match(HTTP_POST, requestUri);
  return plugin != nullptr && plugin->ufn != nullptr;
}

bool WebRouteTableHandler::handle(WebServer& server, const HTTPMethod requestMethod, String requestUri) {
  (void)server;
  if (owner == nullptr) {
    return false;
  }
  const char* uri = requestUri.c_str();
  if (const CoreRoute* core = matchCore(requestMethod, uri)) {
    core->fn(owner);
    return true;
  }
  const core::WebRouteSpec* plugin = core::WebRouteRegistry::match(requestMethod, uri);
  if (plugin == nullptr || plugin->fn == nullptr || owner->server == nullptr) {
    return false;
  }
  plugin->fn(owner->server.get());
  return true;
}

void WebRouteTableHandler::upload(WebServer& server, String requestUri, HTTPUpload& upload) {
  (void)server;
  (void)upload;
  if (owner == nullptr) {
    return;
  }
  const char* uri = requestUri.c_str();
  if (const CoreRoute* core = matchCore(HTTP_POST, uri)) {
    if (core->ufn != nullptr) {
      core->ufn(owner);
    }
    return;
  }
  const core::WebRouteSpec* plugin = core::WebRouteRegistry::match(HTTP_POST, uri);
  if (plugin != nullptr && plugin->ufn != nullptr && owner->server != nullptr) {
    plugin->ufn(owner->server.get());
  }
}

const WebRouteTableHandler::CoreRoute WebRouteTableHandler::kCore[] = {
    {"/", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleRoot>, nullptr},
    {"/files", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFileList>, nullptr},
    {"/js/jszip.min.js", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleJszip>, nullptr},
    {"/api/status", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleStatus>, nullptr},
    {"/api/plugins", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handlePlugins>, nullptr},
    {"/api/todo/entry", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleTodoEntry>, nullptr},
    {"/api/todo/today", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleTodoTodayGet>, nullptr},
    {"/api/todo/today", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleTodoTodaySave>, nullptr},
    {"/api/notes/entry", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleNotesEntry>, nullptr},
    {"/api/notes", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleNotesGet>, nullptr},
    {"/api/notes", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleNotesSave>, nullptr},
    {"/api/files", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFileListData>, nullptr},
    {"/download", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleDownload>, nullptr},
    {"/upload", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleUploadPost>, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleUpload>},
    {"/mkdir", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleCreateFolder>, nullptr},
    {"/rename", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleRename>, nullptr},
    {"/move", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleMove>, nullptr},
    {"/delete", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleDelete>, nullptr},
    {"/settings", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleSettingsPage>, nullptr},
    {"/api/settings", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleGetSettings>, nullptr},
    {"/api/settings", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handlePostSettings>, nullptr},
    {"/fonts", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFontsPage>, nullptr},
    {"/api/fonts", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFontList>, nullptr},
    {"/api/fonts/upload", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFontUpload>,
     &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFontUploadData>},
    {"/api/fonts/delete", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleFontDelete>, nullptr},
    {"/opds", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleOpdsPage>, nullptr},
    {"/api/opds", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleGetOpdsServers>, nullptr},
    {"/api/opds", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handlePostOpdsServer>, nullptr},
    {"/api/opds/delete", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleDeleteOpdsServer>, nullptr},
    {"/api/opds/test", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleTestOpdsServer>, nullptr},
    {"/api/koreader/use-opds", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleKoreaderUseOpds>, nullptr},
    {"/api/book-progress", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleGetBookProgress>, nullptr},
    {"/api/recent", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleRecentBooks>, nullptr},
    {"/api/cover", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleCover>, nullptr},
    {"/api/sleep-images", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleSleepImages>, nullptr},
    {"/api/sleep-cover", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleSleepCoverGet>, nullptr},
    {"/api/sleep-cover/pin", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleSleepCoverPin>, nullptr},
#if ENABLE_REMOTE_CONTROL
    {"/api/open-book", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleOpenBook>, nullptr},
#endif
    {"/api/settings/raw", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleGetSettingsRaw>, nullptr},
#if ENABLE_REMOTE_CONTROL
    {"/api/remote/button", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleRemoteButton>, nullptr},
#endif
    {"/api/screenshot", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleScreenshot>, nullptr},
#if ENABLE_WIFI_CLOCK
    {"/api/time", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleSetTime>, nullptr},
#endif
    {"/api/wifi", HTTP_GET, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleGetWifiNetworks>, nullptr},
    {"/api/wifi", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handlePostWifiNetwork>, nullptr},
    {"/api/wifi/delete", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleDeleteWifiNetwork>, nullptr},
    {"/api/wifi/forget-all", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleForgetAllWifiNetworks>, nullptr},
    {"/api/maintenance/validate-sleep-images", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleMaintenanceValidateSleepImages>,
     nullptr},
    {"/api/maintenance/clear-cache", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleMaintenanceClearCache>, nullptr},
    {"/api/maintenance/reset-settings", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleMaintenanceResetSettings>, nullptr},
    {"/api/maintenance/clear-logs", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleMaintenanceClearLogs>, nullptr},
    {"/api/maintenance/clear-crashes", HTTP_POST, &WebRouteTableHandler::invoke<&CrossPointWebServer::handleMaintenanceClearCrashes>, nullptr},
};

const size_t WebRouteTableHandler::kCoreCount = sizeof(WebRouteTableHandler::kCore) / sizeof(WebRouteTableHandler::kCore[0]);
