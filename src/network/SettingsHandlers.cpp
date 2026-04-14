#include "CrossPointWebServer.h"

#include <Logging.h>

#include "network/SettingsApi.h"

void CrossPointWebServer::handleGetSettings() const {
  server->send(200, "application/json", network::buildSettingsListJson());
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
