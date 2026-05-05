#include <Arduino.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "HostStorage.h"
#include "HostWebServer.h"
#include "network/CoreWebRoutes.h"
#include "network/SettingsSnapshotApi.h"
#include "src/CrossPointSettings.h"

MockESP ESP;
CrossPointSettings CrossPointSettings::instance;

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

std::string parseHtmlRoot(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strncmp(argv[i], "--html-root=", 12) == 0) return argv[i] + 12;
    if (std::strcmp(argv[i], "--html-root") == 0 && i + 1 < argc) return argv[i + 1];
  }
  return {};
}

std::string loadThemeTokens(const std::string& htmlRoot) {
  std::ifstream f(htmlRoot + "/../../../scripts/theme.css");
  if (!f) return {};
  const std::string css((std::istreambuf_iterator<char>(f)), {});
  const size_t open = css.find('{');
  const size_t close = css.rfind('}');
  if (open == std::string::npos || close == std::string::npos) return {};
  return css.substr(open + 1, close - open - 1);
}

std::string injectThemeTokens(std::string html, const std::string& tokens) {
  static constexpr std::string_view kStart = "/* THEME_TOKENS_START */";
  static constexpr std::string_view kEnd = "/* THEME_TOKENS_END */";
  const size_t s = html.find(kStart);
  if (s == std::string::npos) return html;
  const size_t e = html.find(kEnd, s);
  if (e == std::string::npos) return html;
  html.replace(s + kStart.size(), e - (s + kStart.size()), tokens);
  return html;
}

void serveHtmlFile(HostWebServer& server, const std::string& htmlRoot, const char* filename,
                   const std::string& themeTokens) {
  std::ifstream f(htmlRoot + "/" + filename, std::ios::binary);
  if (!f) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  std::string body((std::istreambuf_iterator<char>(f)), {});
  if (!themeTokens.empty()) body = injectThemeTokens(std::move(body), themeTokens);
  server.send_P(200, "text/html; charset=utf-8", body.data(), body.size());
}

void handleSignal(int) {
  if (gServer) gServer->stop();
}

}  // namespace

int main(int argc, char** argv) {
  const int port = parsePort(argc, argv);
  const std::string root = parseRoot(argc, argv);
  const std::string htmlRoot = parseHtmlRoot(argc, argv);
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

  network::CoreWebRouteOptions routeOptions;
  routeOptions.settingsSnapshot = [] { return network::buildSettingsSnapshotJson(SETTINGS); };
  network::mountCoreWebRoutes(server, routeOptions);

  const std::string themeTokens = htmlRoot.empty() ? std::string{} : loadThemeTokens(htmlRoot);
  if (!htmlRoot.empty()) {
    server.on("/", HTTP_GET, [&] { serveHtmlFile(server, htmlRoot, "HomePage.html", themeTokens); });
    server.on("/files", HTTP_GET, [&] { serveHtmlFile(server, htmlRoot, "FilesPage.html", themeTokens); });
    server.on("/settings", HTTP_GET, [&] { serveHtmlFile(server, htmlRoot, "SettingsPage.html", themeTokens); });
    server.serveDirectory(htmlRoot.c_str());
  }

  server.onNotFound([&server] { server.send(404, "text/plain", "Not found"); });

  std::cout << "HostWebServer listening on http://127.0.0.1:" << port << '\n';
  if (!htmlRoot.empty()) std::cout << "  HTML served from " << htmlRoot << '\n';
  const bool ok = server.listen("127.0.0.1", port);
  gServer = nullptr;
  return ok ? 0 : 1;
}
