#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include <functional>

#include "network/FileRoutes.h"

struct CrossPointSettings;

namespace network {

using SettingsSnapshotProvider = std::function<String()>;

struct CoreWebRouteOptions {
  FileRouteOptions fileRoutes;
  SettingsSnapshotProvider settingsSnapshot;
};

void mountCoreWebRoutes(WebServer& server, CoreWebRouteOptions options);

}  // namespace network
