#include <ArduinoJson.h>

#include "HostStorage.h"
#include "HostWebServer.h"
#include "network/FileMutationApi.h"
#include "network/FileReadApi.h"
#include "network/UploadApi.h"
#include "util/PathUtils.h"

namespace {

struct RequestArg {
  bool present = false;
  String value;
};

void sendText(HostWebServer& server, int statusCode, const String& body) {
  server.send(statusCode, "text/plain", body);
}

RequestArg requestArg(HostWebServer& server, const char* name) {
  if (server.hasArg(name)) return {true, server.arg(name)};
  if (!server.hasArg("plain")) return {};

  const String plain = server.arg("plain");
  if (plain.startsWith("{") || plain.startsWith("[")) return {};

  const std::string body = plain.toStdString();
  size_t start = 0;
  while (start <= body.size()) {
    const size_t end = body.find('&', start);
    const std::string_view pair(body.data() + start, (end == std::string::npos ? body.size() : end) - start);
    const size_t eq = pair.find('=');
    const std::string key = std::string(pair.substr(0, eq));
    const std::string value = eq == std::string_view::npos ? std::string() : std::string(pair.substr(eq + 1));
    if (PathUtils::urlDecode(String(key.c_str())) == name) {
      return {true, PathUtils::urlDecode(String(value.c_str()))};
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

bool parseJsonBody(HostWebServer& server, JsonDocument& body, const char* missingMessage, const char* invalidMessage) {
  if (!server.hasArg("plain")) {
    sendText(server, 400, missingMessage);
    return false;
  }
  const String plain = server.arg("plain");
  if (deserializeJson(body, plain.c_str())) {
    sendText(server, 400, invalidMessage);
    return false;
  }
  return true;
}

void invalidatePath(const String&) {}

void handleFileList(HostWebServer& server) {
  const auto path = requestArg(server, "path");
  const auto result = network::buildFileListJson(path.present ? path.value : String(), false);
  server.send(result.statusCode, result.contentType, result.body);
}

void handleDownload(HostWebServer& server) {
  if (!server.hasArg("path")) {
    sendText(server, 400, "Missing path");
    return;
  }

  const auto result = network::resolveDownload(server.arg("path"));
  if (!result.ok()) {
    server.send(result.statusCode, result.contentType, result.body);
    return;
  }

  FsFile file = Storage.open(result.normalizedPath.c_str());
  if (!file || file.isDirectory()) {
    sendText(server, 500, "Failed to open file");
    return;
  }

  server.setContentLength(result.fileSize);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
  server.send(result.statusCode, result.contentType, "");

  uint8_t buffer[4096];
  while (file.available() > 0) {
    const int bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;
    server.sendContent(reinterpret_cast<const char*>(buffer), static_cast<size_t>(bytesRead));
  }
  file.close();
}

void handleMkdir(HostWebServer& server) {
  const auto name = requestArg(server, "name");
  if (!name.present) {
    sendText(server, 400, "Missing folder name");
    return;
  }
  const auto path = requestArg(server, "path");
  const auto result = network::createFolder(path.present ? path.value : String(), name.value);
  sendText(server, result.statusCode, result.body);
}

void handleRename(HostWebServer& server) {
  String itemPath;
  String target;
  bool treatTargetAsName = false;

  const auto formPath = requestArg(server, "path");
  const auto formName = requestArg(server, "name");
  if (formPath.present && formName.present) {
    itemPath = formPath.value;
    target = formName.value;
    treatTargetAsName = true;
  } else {
    JsonDocument body;
    if (!parseJsonBody(server, body, "Missing path or new name", "Invalid JSON body")) return;
    itemPath = String(body["from"] | "");
    target = String(body["to"] | "");
    treatTargetAsName = target.indexOf('/') < 0 && target.indexOf('\\') < 0;
  }

  const auto result = network::renameFile(itemPath, target, treatTargetAsName, invalidatePath);
  sendText(server, result.statusCode, result.body);
}

void handleMove(HostWebServer& server) {
  String itemPath;
  String target;

  const auto formPath = requestArg(server, "path");
  const auto formDest = requestArg(server, "dest");
  if (formPath.present && formDest.present) {
    itemPath = formPath.value;
    target = formDest.value;
  } else {
    JsonDocument body;
    if (!parseJsonBody(server, body, "Missing path or destination", "Invalid JSON body")) return;
    itemPath = String(body["from"] | "");
    target = String(body["to"] | "");
  }

  const auto result = network::moveFile(itemPath, target, invalidatePath);
  sendText(server, result.statusCode, result.body);
}

void handleDelete(HostWebServer& server) {
  const auto path = requestArg(server, "path");
  const auto pathsArg = requestArg(server, "paths");
  const bool hasPathArg = path.present;
  const bool hasPathsArg = pathsArg.present;
  if (!(hasPathArg || hasPathsArg)) {
    sendText(server, 400, "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    sendText(server, 400, "Provide either 'path' or 'paths', not both");
    return;
  }

  std::vector<String> paths;
  if (hasPathArg) {
    paths.push_back(path.value);
  } else {
    JsonDocument body;
    if (deserializeJson(body, pathsArg.value.c_str())) {
      sendText(server, 400, "Invalid paths format");
      return;
    }
    const auto array = body.as<JsonArray>();
    if (array.isNull()) {
      sendText(server, 400, "Invalid paths format");
      return;
    }
    for (const auto& value : array) paths.push_back(String(value.as<const char*>() ? value.as<const char*>() : ""));
  }

  const auto result = network::deletePaths(paths, invalidatePath);
  sendText(server, result.statusCode, result.body);
}

void handleUploadPost(HostWebServer& server) {
  const auto result = network::buildUploadPostResult();
  network::resetUploadSession();
  sendText(server, result.statusCode, result.body);
}

void handleUploadStream(HostWebServer& server) { network::startUpload(&server); }

}  // namespace

void registerFileRoutes(HostWebServer& server) {
  server.on("/api/files", HTTP_GET, [&server] { handleFileList(server); });
  server.on("/download", HTTP_GET, [&server] { handleDownload(server); });
  server.on("/mkdir", HTTP_POST, [&server] { handleMkdir(server); });
  server.on("/rename", HTTP_POST, [&server] { handleRename(server); });
  server.on("/move", HTTP_POST, [&server] { handleMove(server); });
  server.on("/delete", HTTP_POST, [&server] { handleDelete(server); });
  server.on("/upload", HTTP_POST, [&server] { handleUploadPost(server); }, [&server] { handleUploadStream(server); });
}
