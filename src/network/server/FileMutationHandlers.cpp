#include <ArduinoJson.h>
#include <Logging.h>

#include <vector>

#include "network/server/CrossPointWebServer.h"
#include "network/server/CacheInvalidation.h"
#include "network/server/FileMutationApi.h"

void CrossPointWebServer::handleCreateFolder() const {
  if (server->hasArg("name") || server->hasArg("path")) {
    server->send(400, "text/plain", "Use JSON name/path body");
    return;
  }
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument body;
  if (deserializeJson(body, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON body");
    return;
  }

  const String name = body["name"].as<String>();
  if (name.isEmpty()) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }
  const String path = body["path"] | "/";

  const auto result = network::createFolder(path, name);
  if (result.ok()) {
    LOG_DBG("WEB", "%s", result.body.c_str());
  } else if (result.statusCode >= 500) {
    LOG_DBG("WEB", "Failed mkdir for path=%s name=%s", path.c_str(), name.c_str());
  }
  server->send(result.statusCode, "text/plain", result.body);
}

void CrossPointWebServer::handleRename() const {
  if (server->hasArg("path") || server->hasArg("name")) {
    server->send(400, "text/plain", "Use JSON from/to body");
    return;
  }
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument body;
  if (deserializeJson(body, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON body");
    return;
  }

  const String itemPath = body["from"].as<String>();
  const String renameTarget = body["to"].as<String>();
  const bool treatTargetAsName = renameTarget.indexOf('/') < 0 && renameTarget.indexOf('\\') < 0;
  const auto result = network::renameFile(itemPath, renameTarget, treatTargetAsName,
                                          [](const String& path) { invalidateFeatureCachesIfNeeded(path); });
  server->send(result.statusCode, "text/plain", result.body);
}

void CrossPointWebServer::handleMove() const {
  if (server->hasArg("path") || server->hasArg("dest")) {
    server->send(400, "text/plain", "Use JSON from/to body");
    return;
  }
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument body;
  if (deserializeJson(body, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON body");
    return;
  }

  const String itemPath = body["from"].as<String>();
  const String destPath = body["to"].as<String>();
  const auto result =
      network::moveFile(itemPath, destPath, [](const String& path) { invalidateFeatureCachesIfNeeded(path); });
  server->send(result.statusCode, "text/plain", result.body);
}

void CrossPointWebServer::handleDelete() const {
  if (server->hasArg("path") || server->hasArg("paths")) {
    server->send(400, "text/plain", "Use paths JSON array");
    return;
  }

  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  std::vector<String> paths;
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON body");
    return;
  }

  JsonArray jsonPaths = doc.as<JsonArray>();
  if (jsonPaths.isNull() || jsonPaths.size() == 0) {
    server->send(400, "text/plain", jsonPaths.isNull() ? "Use paths JSON array" : "No paths provided");
    return;
  }

  paths.reserve(jsonPaths.size());
  for (const auto& p : jsonPaths) {
    paths.push_back(p.as<String>());
  }

  const auto result = network::deletePaths(paths, [](const String& path) { invalidateFeatureCachesIfNeeded(path); });
  server->send(result.statusCode, "text/plain", result.body);
}
