#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "CrossPointWebServer.h"
#include "core/features/FeatureModules.h"
#include "network/SettingsSnapshotApi.h"

namespace {
constexpr uint8_t CROSSPOINT_STATUS_PROTOCOL_VERSION = 1;
}

void CrossPointWebServer::handleStatus() const {
  requestCount++;
  noteWebUiAccess();
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  const bool staConnected = WiFi.status() == WL_CONNECTED;
  const String wifiStatus = apMode ? "AP Mode" : (staConnected ? "Connected" : "Disconnected");

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["protocolVersion"] = CROSSPOINT_STATUS_PROTOCOL_VERSION;
  doc["wifiStatus"] = wifiStatus;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["openBook"] = APP_STATE.openEpubPath.c_str();
  doc["otaSelectedBundle"] = SETTINGS.selectedOtaBundle;
  doc["otaInstalledBundle"] = SETTINGS.installedOtaBundle;
  doc["otaInstalledFeatures"] = SETTINGS.installedOtaFeatureFlags[0] != '\0' ? SETTINGS.installedOtaFeatureFlags
                                                                             : core::FeatureModules::getBuildString();

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handlePlugins() const {
  server->send(200, "application/json", core::FeatureModules::getFeatureMapJson());
}

void CrossPointWebServer::handleGetSettingsRaw() const {
  server->send(200, "application/json", network::buildSettingsSnapshotJson(SETTINGS));
}
