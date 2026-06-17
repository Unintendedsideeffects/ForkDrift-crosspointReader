#include "network/server/CoreWebRoutes.h"

#include "network/server/SettingsSnapshotApi.h"

namespace network {

void mountCoreWebRoutes(WebServer& server, CoreWebRouteOptions options) {
  if (options.settingsSnapshot) {
    server.on("/api/settings/raw", HTTP_GET,
              [&server, options] { server.send(200, "application/json", options.settingsSnapshot()); });
  }

  mountFileRoutes(server, options.fileRoutes);
}

}  // namespace network
