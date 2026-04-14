#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <vector>

#include "network/CacheInvalidation.h"
#include "network/FileMutationApi.h"

void CrossPointWebServer::handleCreateFolder() const {
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const auto result = network::createFolder(server->hasArg("path") ? server->arg("path") : "/", server->arg("name"));
  if (result.ok()) {
    LOG_DBG("WEB", "%s", result.body.c_str());
  } else if (result.statusCode >= 500) {
    LOG_DBG("WEB", "Failed mkdir for path=%s name=%s", server->arg("path").c_str(), server->arg("name").c_str());
  }
  server->send(result.statusCode, "text/plain", result.body);
}

void CrossPointWebServer::handleRename() const {
  String itemPath;
  String renameTarget;
  bool fromFormContract = false;

  if (server->hasArg("path") && server->hasArg("name")) {
    itemPath = server->arg("path");
    renameTarget = server->arg("name");
    fromFormContract = true;
  } else {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing path or new name");
      return;
    }

    JsonDocument body;
    if (deserializeJson(body, server->arg("plain"))) {
      server->send(400, "text/plain", "Invalid JSON body");
      return;
    }

    itemPath = body["from"].as<String>();
    renameTarget = body["to"].as<String>();
  }

  const bool treatTargetAsName = fromFormContract || (renameTarget.indexOf('/') < 0 && renameTarget.indexOf('\\') < 0);
  const auto result = network::renameFile(itemPath, renameTarget, treatTargetAsName,
                                          [](const String& path) { invalidateFeatureCachesIfNeeded(path); });
  server->send(result.statusCode, "text/plain", result.body);
}

void CrossPointWebServer::handleMove() const {
  String itemPath;
  String destPath;

  if (server->hasArg("path") && server->hasArg("dest")) {
    itemPath = server->arg("path");
    destPath = server->arg("dest");
  } else {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing path or destination");
      return;
    }

    JsonDocument body;
    if (deserializeJson(body, server->arg("plain"))) {
      server->send(400, "text/plain", "Invalid JSON body");
      return;
    }

    itemPath = body["from"].as<String>();
    destPath = body["to"].as<String>();
  }

  const auto result =
      network::moveFile(itemPath, destPath, [](const String& path) { invalidateFeatureCachesIfNeeded(path); });
  server->send(result.statusCode, "text/plain", result.body);
}

void CrossPointWebServer::handleDelete() const {
  const bool hasPathArg = server->hasArg("path");
  const bool hasPathsArg = server->hasArg("paths");

  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  std::vector<String> paths;

  if (hasPathsArg) {
    JsonDocument doc;
    if (deserializeJson(doc, server->arg("paths"))) {
      server->send(400, "text/plain", "Invalid paths format");
      return;
    }

    JsonArray jsonPaths = doc.as<JsonArray>();
    if (jsonPaths.isNull() || jsonPaths.size() == 0) {
      server->send(400, "text/plain", "No paths provided");
      return;
    }

    paths.reserve(jsonPaths.size());
    for (const auto& p : jsonPaths) {
      paths.push_back(p.as<String>());
    }
  } else {
    paths.push_back(server->arg("path"));
  }

  const auto result =
      network::deletePaths(paths, [](const String& path) { invalidateFeatureCachesIfNeeded(path); });
  server->send(result.statusCode, "text/plain", result.body);
}
