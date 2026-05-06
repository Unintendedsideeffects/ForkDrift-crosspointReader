#include "network/FileReadApi.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#define FILEREAD_HAS_TASK_WDT 1
#else
#define FILEREAD_HAS_TASK_WDT 0
#endif

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

bool streamFileListJson(WebServer& server, const String& rawPath, const bool showHiddenFiles, String* normalizedPath,
                        size_t* entryCount) {
  const auto pathResult = resolveFileListPath(rawPath);
  if (!pathResult.ok()) {
    server.send(pathResult.statusCode, pathResult.contentType, pathResult.body);
    return false;
  }

  if (normalizedPath) {
    *normalizedPath = pathResult.normalizedPath;
  }

  constexpr size_t kBatchCapacity = 2048;
  constexpr size_t kJsonMax = 512;
  char* const batch = static_cast<char*>(malloc(kBatchCapacity));
  if (!batch) {
    LOG_ERR("WEB", "File list: failed to malloc %u byte batch buffer", static_cast<unsigned>(kBatchCapacity));
    server.send(500, "text/plain", "Insufficient memory");
    return false;
  }

  size_t batchLen = 0;
  bool seenFirst = false;
  size_t sentEntries = 0;
  char jsonBuf[kJsonMax];
  JsonDocument doc;

  auto flushBatch = [&]() {
    if (batchLen == 0) {
      return;
    }
#if FILEREAD_HAS_TASK_WDT
    esp_task_wdt_reset();
#endif
    server.sendContent(batch, batchLen);
    batchLen = 0;
#if defined(ARDUINO)
    yield();
#if FILEREAD_HAS_TASK_WDT
    esp_task_wdt_reset();
#endif
#endif
  };

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");

  scanDirectory(pathResult.normalizedPath.c_str(), showHiddenFiles, [&](const DirEntry& entry) {
    doc.clear();
    doc["name"] = entry.name;
    doc["size"] = entry.size;
    doc["isDirectory"] = entry.isDirectory;
    doc["isEpub"] = entry.isEpub;
    doc["modified"] = 0;

    const size_t jsonLen = serializeJson(doc, jsonBuf, kJsonMax);
    if (jsonLen >= kJsonMax) {
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", entry.name.c_str());
      return;
    }

    const size_t needed = jsonLen + (seenFirst ? 1 : 0);
    if (batchLen + needed > kBatchCapacity) {
      flushBatch();
    }

    if (seenFirst) {
      batch[batchLen++] = ',';
    } else {
      seenFirst = true;
    }

    memcpy(batch + batchLen, jsonBuf, jsonLen);
    batchLen += jsonLen;
    sentEntries++;
  });

  flushBatch();
  free(batch);

  server.sendContent("]");
  server.sendContent("");

  if (entryCount) {
    *entryCount = sentEntries;
  }
  return true;
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
