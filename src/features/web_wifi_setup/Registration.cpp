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

namespace features::web_wifi_setup {
namespace {

#if ENABLE_WEB_WIFI_SETUP
bool shouldRegisterWebWifiSetupApiRoute() { return core::FeatureCatalog::isEnabled("web_wifi_setup"); }

void mountWifiRoutes(WebServer* server) {
  server->on("/api/wifi/scan", HTTP_GET, [server] {
    // Non-blocking scan: start async scan on first call and return HTTP 202
    // immediately. The browser re-polls until it receives HTTP 200 with results.
    // This prevents blocking the main task (and thus the display) for the
    // several seconds a WiFi scan takes.
    static bool scanActive = false;
    const int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      // Scan complete — build and return results.
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
    // n == WIFI_SCAN_FAILED: either not yet started or a previous scan failed.
    if (scanActive) {
      // We started a scan but it failed.
      scanActive = false;
      server->send(500, "text/plain", "WiFi scan failed");
      return;
    }
    // Start a fresh async scan.
    WiFi.scanNetworks(/*async=*/true);
    scanActive = true;
    server->send(202, "application/json", "{\"scanning\":true}");
  });

  server->on("/api/wifi/connect", HTTP_POST, [server] {
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
  });

  server->on("/api/wifi/forget", HTTP_POST, [server] {
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
  });

  server->on("/api/wifi/status", HTTP_GET, [server] {
    JsonDocument doc;
    const wl_status_t wifiSt = WiFi.status();
    const bool connected = wifiSt == WL_CONNECTED;
    doc["connected"] = connected;
    if (WiFi.getMode() == WIFI_MODE_AP) {
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
  });
}
#endif

}  // namespace

void registerFeature() {
#if ENABLE_WEB_WIFI_SETUP
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "web_wifi_setup_api";
  webRouteEntry.shouldRegister = shouldRegisterWebWifiSetupApiRoute;
  webRouteEntry.mountRoutes = mountWifiRoutes;
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::web_wifi_setup
