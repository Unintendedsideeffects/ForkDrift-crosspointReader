#include <Arduino.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "HostStorage.h"
#include "HostWebServer.h"
#include "network/SettingsSnapshotApi.h"
#include "src/CrossPointSettings.h"

MockESP ESP;
CrossPointSettings CrossPointSettings::instance;

void registerFileRoutes(HostWebServer& server);

namespace {

HostWebServer* gServer = nullptr;

int parsePort(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--port=", 7) == 0) return std::atoi(argv[i] + 7);
    if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) return std::atoi(argv[i + 1]);
  }
  return 8080;
}

std::string parseRoot(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--root=", 7) == 0) return argv[i] + 7;
    if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) return argv[i + 1];
  }
  return {};
}

void handleSignal(int) {
  if (gServer) gServer->stop();
}

}  // namespace

int main(int argc, char** argv) {
  const int port = parsePort(argc, argv);
  const std::string root = parseRoot(argc, argv);
  if (port <= 0 || port > 65535) {
    std::cerr << "invalid --port value\n";
    return 2;
  }
  if (root.empty() || !Storage.setRoot(root)) {
    std::cerr << "invalid --root value\n";
    return 2;
  }

  auto& settings = SETTINGS;
  std::strncpy(settings.deviceName, "host-shim", sizeof(settings.deviceName) - 1);
  settings.deviceName[sizeof(settings.deviceName) - 1] = '\0';

  HostWebServer server;
  gServer = &server;
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  server.on("/api/settings/raw", HTTP_GET,
            [&server] { server.send(200, "application/json", network::buildSettingsSnapshotJson(SETTINGS)); });
  registerFileRoutes(server);
  server.onNotFound([&server] { server.send(404, "text/plain", "Not found"); });

  std::cout << "HostWebServer listening on http://127.0.0.1:" << port << '\n';
  const bool ok = server.listen("127.0.0.1", port);
  gServer = nullptr;
  return ok ? 0 : 1;
}
