#include "features/web_wifi_setup/Registration.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>
#include <WiFi.h>

#include "SpiBusMutex.h"
#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "util/WifiCredentialStore.h"
#include "util/WifiScanPolicy.h"

namespace features::web_wifi_setup {
namespace {

#if ENABLE_WEB_WIFI_SETUP
bool shouldRegisterWebWifiSetupApiRoute() { return core::FeatureCatalog::isEnabled("web_wifi_setup"); }

void handleWifiScan(WebServer* server) {
    if (WiFi.getMode() & WIFI_MODE_AP) {
      server->send(409, "application/json", "{\"error\":\"scan unavailable in access-point mode\"}");
      return;
    }

    static bool scanActive = false;
    const int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      scanActive = false;
      const bool staConnected = (WiFi.getMode() & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
      const String activeSsid = staConnected ? WiFi.SSID() : String();
      JsonDocument doc;
      JsonArray array = doc.to<JsonArray>();
      for (int i = 0; i < n; ++i) {
        const String networkSsid = WiFi.SSID(i);
        const bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        JsonObject obj = array.add<JsonObject>();
        obj["ssid"] = networkSsid;
        obj["rssi"] = WiFi.RSSI(i);
        obj["secured"] = secured;
        obj["saved"] = WIFI_STORE.hasSavedCredential(networkSsid.c_str());
        obj["connected"] = staConnected && (networkSsid == activeSsid);
      }
      WiFi.scanDelete();
      String json;
      serializeJson(doc, json);
      server->send(200, "application/json", json);
      return;
    }
    if (n == WIFI_SCAN_RUNNING) {
      server->send(202, "application/json", "{\"scanning\":true}");
      return;
    }
    if (scanActive) {
      scanActive = false;
      server->send(500, "text/plain", "WiFi scan failed");
      return;
    }
    startWifiScanAsync();
    scanActive = true;
    server->send(202, "application/json", "{\"scanning\":true}");
}

void handleWifiConnect(WebServer* server) {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing body");
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server->arg("plain"));
    String ssid = doc["ssid"];
    String password = doc["password"];
    if (ssid.length() == 0) {
      server->send(400, "text/plain", "SSID required");
      return;
    }
    WIFI_STORE.addCredential(ssid.c_str(), password.c_str());
    bool saved = false;
    {
      SpiBusMutex::Guard guard;
      saved = WIFI_STORE.saveToFile();
    }
    if (!saved) {
      server->send(500, "text/plain", "Failed to save WiFi credentials");
      return;
    }
    server->send(200, "text/plain", "WiFi credentials saved");
}

void handleWifiForget(WebServer* server) {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing body");
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server->arg("plain"));
    String ssid = doc["ssid"];
    if (ssid.length() > 0) {
      WIFI_STORE.removeCredential(ssid.c_str());
      bool saved = false;
      {
        SpiBusMutex::Guard guard;
        saved = WIFI_STORE.saveToFile();
      }
      if (!saved) {
        server->send(500, "text/plain", "Failed to remove WiFi credentials");
        return;
      }
      server->send(200, "text/plain", "WiFi credentials removed");
    } else {
      server->send(400, "text/plain", "SSID required");
    }
}

void handleWifiStatus(WebServer* server) {
    JsonDocument doc;
    const wl_status_t wifiSt = WiFi.status();
    const bool connected = wifiSt == WL_CONNECTED;
    doc["connected"] = connected;
    if (WiFi.getMode() & WIFI_MODE_AP) {
      doc["mode"] = "AP";
      doc["ssid"] = WiFi.softAPSSID();
    } else if (connected) {
      doc["mode"] = "STA";
      doc["ssid"] = WiFi.SSID();
      doc["ip"] = WiFi.localIP().toString();
      doc["rssi"] = WiFi.RSSI();
    } else {
      doc["mode"] = "STA";
      const char* stStr = (wifiSt == WL_CONNECT_FAILED)  ? "failed"
                          : (wifiSt == WL_NO_SSID_AVAIL) ? "no_ssid"
                          : (wifiSt == WL_IDLE_STATUS)   ? "connecting"
                                                         : "disconnected";
      doc["status"] = stStr;
    }
    String json;
    serializeJson(doc, json);
    server->send(200, "application/json", json);
}

const core::WebRouteSpec kWifiRoutes[] = {
    {"/api/wifi/scan", HTTP_GET, handleWifiScan, nullptr},
    {"/api/wifi/connect", HTTP_POST, handleWifiConnect, nullptr},
    {"/api/wifi/forget", HTTP_POST, handleWifiForget, nullptr},
    {"/api/wifi/status", HTTP_GET, handleWifiStatus, nullptr},
};
#endif

}  // namespace

void registerFeature() {
#if ENABLE_WEB_WIFI_SETUP
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "web_wifi_setup_api";
  webRouteEntry.shouldRegister = shouldRegisterWebWifiSetupApiRoute;
  webRouteEntry.routes = kWifiRoutes;
  webRouteEntry.routeCount = sizeof(kWifiRoutes) / sizeof(kWifiRoutes[0]);
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_wifi_setup
