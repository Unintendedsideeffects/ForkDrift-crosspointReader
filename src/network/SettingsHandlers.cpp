#include <Arduino.h>
#include <Logging.h>

#include "CrossPointWebServer.h"
#include "network/SettingsApi.h"
#include "util/AgentDebugLog.h"

void CrossPointWebServer::handleGetSettings() const {
  // #region agent log
  {
    char data[120];
    snprintf(data, sizeof(data), "{\"heap\":%u}", static_cast<unsigned int>(ESP.getFreeHeap()));
    agentDebugLog("initial", "H4,H5,H6", "SettingsHandlers.cpp:handleGetSettings", "settings API handler entry", data);
  }
  // #endregion
  const String settingsJson = network::buildSettingsListJson();
  server->send(200, "application/json", settingsJson);
  // #region agent log
  {
    char data[160];
    snprintf(data, sizeof(data), "{\"heap\":%u,\"bytes\":%u}", static_cast<unsigned int>(ESP.getFreeHeap()),
             static_cast<unsigned int>(settingsJson.length()));
    agentDebugLog("initial", "H4,H5,H6", "SettingsHandlers.cpp:handleGetSettings", "settings API handler exit", data);
  }
  // #endregion
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
