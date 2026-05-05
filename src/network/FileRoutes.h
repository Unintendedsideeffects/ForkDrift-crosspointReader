#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include <functional>

namespace network {

using FileRouteInvalidationCallback = std::function<void(const String&)>;

struct FileRouteOptions {
  bool showHiddenFiles = false;
  FileRouteInvalidationCallback onPathChanged;
};

void mountFileRoutes(WebServer& server, FileRouteOptions options = {});

}  // namespace network
