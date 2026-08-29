#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointState.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/html/FilesPageHtml.generated.h"
#include "network/html/HomePageHtml.generated.h"
#include "network/html/OpdsPageHtml.generated.h"
#include "network/html/SettingsPageHtml.generated.h"
#include "network/html/js/jszip_minJs.generated.h"
#include "network/server/CrossPointWebServer.h"
#include "network/server/WebUtils.h"

static_assert(HomePageHtmlCompressedSize == sizeof(HomePageHtml), "Home page compressed size mismatch");
static_assert(FilesPageHtmlCompressedSize == sizeof(FilesPageHtml), "Files page compressed size mismatch");
static_assert(SettingsPageHtmlCompressedSize == sizeof(SettingsPageHtml), "Settings page compressed size mismatch");
static_assert(OpdsPageHtmlCompressedSize == sizeof(OpdsPageHtml), "OPDS page compressed size mismatch");

namespace {

bool shouldRegisterHealthRoute() { return true; }

void handleHealth(WebServer* server) { server->send(200, "application/json", "{\"status\":\"ok\"}"); }

const core::WebRouteSpec kHealthRoutes[] = {
    {"/health", HTTP_GET, handleHealth, nullptr},
};

struct HealthRouteRegistration {
  HealthRouteRegistration() {
    core::WebRouteEntry entry{};
    entry.routeId = "health";
    entry.shouldRegister = shouldRegisterHealthRoute;
    entry.routes = kHealthRoutes;
    entry.routeCount = sizeof(kHealthRoutes) / sizeof(kHealthRoutes[0]);
    core::WebRouteRegistry::add(entry);
  }
};

HealthRouteRegistration healthRouteRegistration;

}  // namespace

void CrossPointWebServer::handleRoot() const {
  noteWebUiAccess();
  sendPrecompressedHtml(server.get(), HomePageHtml, HomePageHtmlCompressedSize);
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", jszip_minJs, jszip_minJsCompressedSize);
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleNotFound() const {
  if (apMode) {
    const String redirectUrl = "http://" + WiFi.softAPIP().toString() + apRedirectPath.c_str();
    server->sendHeader("Location", redirectUrl);
    server->send(302, "text/plain", "");
    return;
  }
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleFileList() const {
  noteWebUiAccess();
  sendPrecompressedHtml(server.get(), FilesPageHtml, FilesPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served files page");
}

void CrossPointWebServer::handleSettingsPage() const {
  noteWebUiAccess();
  sendPrecompressedHtml(server.get(), SettingsPageHtml, SettingsPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleOpdsPage() const {
  noteWebUiAccess();
  sendPrecompressedHtml(server.get(), OpdsPageHtml, OpdsPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served OPDS page");
}

void CrossPointWebServer::handleScreenshot() {
  APP_STATE.pendingScreenshot = true;
  server->send(202, "application/json", "{\"status\":\"ok\"}");
}
