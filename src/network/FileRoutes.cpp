#include "network/FileRoutes.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <vector>

#include "network/FileMutationApi.h"
#include "network/FileReadApi.h"
#include "network/UploadApi.h"
#include "util/PathUtils.h"

namespace {

struct RequestArg {
  bool present = false;
  String value;
};

void sendText(WebServer& server, const int statusCode, const String& body) {
  server.send(statusCode, "text/plain", body);
}

RequestArg requestArg(WebServer& server, const char* name) {
  if (server.hasArg(name)) return {true, server.arg(name)};
  if (!server.hasArg("plain")) return {};

  const String plain = server.arg("plain");
  if (plain.startsWith("{") || plain.startsWith("[")) return {};

  const std::string body(plain.c_str(), plain.length());
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

bool parseJsonBody(WebServer& server, JsonDocument& body, const char* missingMessage, const char* invalidMessage) {
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

void handleFileList(WebServer& server, const network::FileRouteOptions& options) {
  const auto path = requestArg(server, "path");
  network::streamFileListJson(server, path.present ? path.value : String(), options.showHiddenFiles);
}

void handleDownload(WebServer& server) {
  if (!server.hasArg("path")) {
    sendText(server, 400, "Missing path");
    return;
  }

  const auto result = network::resolveDownload(server.arg("path"));
  if (!result.ok()) {
    server.send(result.statusCode, result.contentType, result.body);
    return;
  }

  HalFile file = Storage.open(result.normalizedPath.c_str());
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

void handleMkdir(WebServer& server) {
  if (requestArg(server, "name").present || requestArg(server, "path").present) {
    sendText(server, 400, "Use JSON name/path body");
    return;
  }

  JsonDocument body;
  if (!parseJsonBody(server, body, "Missing JSON body", "Invalid JSON body")) return;
  const String name = String(body["name"] | "");
  if (name.isEmpty()) {
    sendText(server, 400, "Missing folder name");
    return;
  }
  const String path = String(body["path"] | "/");
  const auto result = network::createFolder(path, name);
  sendText(server, result.statusCode, result.body);
}

void handleRename(WebServer& server, const network::FileRouteOptions& options) {
  if (requestArg(server, "path").present || requestArg(server, "name").present) {
    sendText(server, 400, "Use JSON from/to body");
    return;
  }

  JsonDocument body;
  if (!parseJsonBody(server, body, "Missing JSON body", "Invalid JSON body")) return;
  const String itemPath = String(body["from"] | "");
  const String target = String(body["to"] | "");
  const bool treatTargetAsName = target.indexOf('/') < 0 && target.indexOf('\\') < 0;

  const auto result = network::renameFile(itemPath, target, treatTargetAsName, options.onPathChanged);
  sendText(server, result.statusCode, result.body);
}

void handleMove(WebServer& server, const network::FileRouteOptions& options) {
  if (requestArg(server, "path").present || requestArg(server, "dest").present) {
    sendText(server, 400, "Use JSON from/to body");
    return;
  }

  JsonDocument body;
  if (!parseJsonBody(server, body, "Missing JSON body", "Invalid JSON body")) return;
  const String itemPath = String(body["from"] | "");
  const String target = String(body["to"] | "");

  const auto result = network::moveFile(itemPath, target, options.onPathChanged);
  sendText(server, result.statusCode, result.body);
}

void handleDelete(WebServer& server, const network::FileRouteOptions& options) {
  if (requestArg(server, "path").present || requestArg(server, "paths").present) {
    sendText(server, 400, "Use paths JSON array");
    return;
  }

  std::vector<String> paths;
  JsonDocument body;
  if (!parseJsonBody(server, body, "Missing JSON body", "Invalid JSON body")) return;
  const auto array = body.as<JsonArray>();
  if (array.isNull()) {
    sendText(server, 400, "Use paths JSON array");
    return;
  }
  for (const auto& value : array) paths.push_back(String(value.as<const char*>() ? value.as<const char*>() : ""));

  const auto result = network::deletePaths(paths, options.onPathChanged);
  sendText(server, result.statusCode, result.body);
}

void handleUploadPost(WebServer& server) {
  const auto result = network::buildUploadPostResult();
  network::resetUploadSession();
  sendText(server, result.statusCode, result.body);
}

void handleUploadStream(WebServer& server) { network::startUpload(&server); }

}  // namespace

namespace network {

void mountFileRoutes(WebServer& server, FileRouteOptions options) {
  server.on("/api/files", HTTP_GET, [&server, options] { handleFileList(server, options); });
  server.on("/download", HTTP_GET, [&server] { handleDownload(server); });
  server.on("/mkdir", HTTP_POST, [&server] { handleMkdir(server); });
  server.on("/rename", HTTP_POST, [&server, options] { handleRename(server, options); });
  server.on("/move", HTTP_POST, [&server, options] { handleMove(server, options); });
  server.on("/delete", HTTP_POST, [&server, options] { handleDelete(server, options); });
  server.on("/upload", HTTP_POST, [&server] { handleUploadPost(server); }, [&server] { handleUploadStream(server); });
}

}  // namespace network
