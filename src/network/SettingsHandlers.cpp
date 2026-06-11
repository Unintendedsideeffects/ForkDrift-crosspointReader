#include <Arduino.h>
#include <Logging.h>

#include "CrossPointWebServer.h"
#include "network/SettingsApi.h"

namespace {
// Keep in sync with kMinHeapForSettingsRebuild in SettingsActivity.cpp.
constexpr uint32_t kMinHeapForSettingsList = 48000;
}  // namespace

void CrossPointWebServer::handleGetSettings() const {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForSettingsList) {
    LOG_WRN("WEB", "Settings API rejected at low heap: %u bytes free", freeHeap);
    server->send(503, "application/json", "{\"error\":\"low memory\"}");
    return;
  }

  const String settingsJson = network::buildSettingsListJson();
  server->send(200, "application/json", settingsJson);
  LOG_DBG("WEB", "Served settings API");
}

void CrossPointWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const auto result = network::applySettingsJson(server->arg("plain"));
  if (result.statusCode == 500) {
    LOG_WRN("WEB", "Failed to persist settings to SD card");
  } else if (result.ok()) {
    LOG_DBG("WEB", "Applied %d setting(s)", result.appliedCount);
  }

  server->send(result.statusCode, result.contentType, result.body);
}
