#include "network/FileReadApi.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include "SpiBusMutex.h"
#include "network/FileListApi.h"
#include "util/PathUtils.h"

namespace network {

FileListDescriptor resolveFileListPath(const String& rawPath) {
  String currentPath = "/";
  if (!rawPath.isEmpty()) {
    currentPath = PathUtils::urlDecode(rawPath);
    if (!PathUtils::isValidSdPath(currentPath)) {
      return {400, "text/plain", "Invalid path", ""};
    }

    currentPath = PathUtils::normalizePath(currentPath);
    if (PathUtils::pathContainsProtectedItem(currentPath)) {
      return {403, "text/plain", "Cannot access protected items", ""};
    }
  }

  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(currentPath.c_str());
  }
  if (!exists) {
    return {404, "text/plain", "Item not found", ""};
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(currentPath.c_str());
  }
  if (!file) {
    return {500, "text/plain", "Failed to open directory", ""};
  }

  bool isDirectory = false;
  {
    SpiBusMutex::Guard guard;
    isDirectory = file.isDirectory();
    file.close();
  }
  if (!isDirectory) {
    return {400, "text/plain", "Path is not a directory", ""};
  }

  return {200, "application/json", "", currentPath};
}

FileListDescriptor buildFileListJson(const String& rawPath, const bool showHiddenFiles) {
  const auto pathResult = resolveFileListPath(rawPath);
  if (!pathResult.ok()) {
    return pathResult;
  }

  const String& currentPath = pathResult.normalizedPath;
  String json = "[";
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanDirectory(currentPath.c_str(), showHiddenFiles, [&](const DirEntry& entry) {
    doc.clear();
    doc["name"] = entry.name;
    doc["size"] = entry.size;
    doc["isDirectory"] = entry.isDirectory;
    doc["isEpub"] = entry.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", entry.name.c_str());
      return;
    }

    if (seenFirst) {
      json += ",";
    } else {
      seenFirst = true;
    }
    json += output;
  });

  json += "]";
  return {200, "application/json", json, currentPath};
}

DownloadDescriptor resolveDownload(const String& rawPath) {
  String itemPath = PathUtils::urlDecode(rawPath);
  if (!PathUtils::isValidSdPath(itemPath)) {
    return {400, "text/plain", "Invalid path"};
  }

  itemPath = PathUtils::normalizePath(itemPath);
  if (itemPath.isEmpty() || itemPath == "/") {
    return {400, "text/plain", "Invalid path"};
  }
  if (PathUtils::pathContainsProtectedItem(itemPath)) {
    return {403, "text/plain", "Cannot access protected items"};
  }

  const int lastSlash = itemPath.lastIndexOf('/');
  const String itemName = lastSlash >= 0 ? itemPath.substring(lastSlash + 1) : itemPath;
  // Defence-in-depth: pathContainsProtectedItem() already rejects protected path
  // segments, but isProtectedWebComponent() on the final component catches cases
  // where a caller constructs a path that bypasses the path-level check.
  if (PathUtils::isProtectedWebComponent(itemName)) {
    return {403, "text/plain", "Cannot access protected items"};
  }

  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(itemPath.c_str());
  }
  if (!exists) {
    return {404, "text/plain", "Item not found"};
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(itemPath.c_str());
  }
  if (!file) {
    return {500, "text/plain", "Failed to open file"};
  }

  bool isDirectory = false;
  size_t fileSize = 0;
  char nameBuf[128] = {0};
  {
    SpiBusMutex::Guard guard;
    isDirectory = file.isDirectory();
    if (!isDirectory) {
      fileSize = file.size();
      file.getName(nameBuf, sizeof(nameBuf));
    }
    file.close();
  }
  if (isDirectory) {
    return {400, "text/plain", "Path is a directory"};
  }

  String contentType = "application/octet-stream";
  if (FsHelpers::hasEpubExtension(itemPath)) {
    contentType = "application/epub+zip";
  }

  String filename = nameBuf[0] != '\0' ? String(nameBuf) : String("download");
  return {200, contentType, "", itemPath, filename, fileSize};
}

}  // namespace network
