#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointState.h"
#include "CrossPointWebServer.h"
#include "html/FilesPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "network/WebUtils.h"
#include "util/AgentDebugLog.h"

static_assert(HomePageHtmlCompressedSize == sizeof(HomePageHtml), "Home page compressed size mismatch");
static_assert(FilesPageHtmlCompressedSize == sizeof(FilesPageHtml), "Files page compressed size mismatch");
static_assert(SettingsPageHtmlCompressedSize == sizeof(SettingsPageHtml), "Settings page compressed size mismatch");

void CrossPointWebServer::handleRoot() const {
  // #region agent log
  {
    char data[120];
    snprintf(data, sizeof(data), "{\"heap\":%u,\"uri\":\"/\"}", static_cast<unsigned int>(ESP.getFreeHeap()));
    agentDebugLog("initial", "H4,H5", "StaticHandlers.cpp:handleRoot", "serving root page", data);
  }
  // #endregion
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
    // In AP mode, redirect any unrecognised URL to the home page.
    // OS captive-portal probes (Apple /hotspot-detect.html, Android /generate_204,
    // Windows /ncsi.txt, etc.) all land here because none match a registered route.
    // A 302 to the raw AP IP triggers the "Sign in to network" notification on every
    // major OS; we use the IP rather than the .local hostname because mDNS is blocked
    // on clients until after they dismiss the captive portal.
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
  // #region agent log
  {
    char data[120];
    snprintf(data, sizeof(data), "{\"heap\":%u,\"uri\":\"/files\"}", static_cast<unsigned int>(ESP.getFreeHeap()));
    agentDebugLog("initial", "H4,H5", "StaticHandlers.cpp:handleFileList", "serving files page", data);
  }
  // #endregion
  sendPrecompressedHtml(server.get(), FilesPageHtml, FilesPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served files page");
}

void CrossPointWebServer::handleSettingsPage() const {
  // #region agent log
  {
    char data[120];
    snprintf(data, sizeof(data), "{\"heap\":%u,\"uri\":\"/settings\"}", static_cast<unsigned int>(ESP.getFreeHeap()));
    agentDebugLog("initial", "H4,H5", "StaticHandlers.cpp:handleSettingsPage", "serving settings page", data);
  }
  // #endregion
  sendPrecompressedHtml(server.get(), SettingsPageHtml, SettingsPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleScreenshot() {
  APP_STATE.pendingScreenshot = true;
  server->send(202, "application/json", "{\"status\":\"ok\"}");
}
