#include "network/SleepCoverApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <cstring>
#include <string>

#include "SpiBusMutex.h"
#include "util/PathUtils.h"

namespace network {
namespace {

constexpr const char* kPinnedSleepCoverPath = "/sleep/.pinned-cover.bmp";

SleepCoverHttpResult jsonResponse(const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  return {200, "application/json", json};
}

void setPinnedPath(char* pinnedPath, const size_t pinnedPathSize, const char* value) {
  if (pinnedPath == nullptr || pinnedPathSize == 0) {
    return;
  }
  std::strncpy(pinnedPath, value, pinnedPathSize - 1);
  pinnedPath[pinnedPathSize - 1] = '\0';
}

SleepCoverHttpResult savePinnedPath(char* pinnedPath, const size_t pinnedPathSize, const char* value,
                                    const SleepCoverSaveSettings& saveSettings) {
  // Snapshot the old value on the stack so we can restore in-memory state if persistence fails.
  // pinnedPathSize is always <= sizeof(CrossPointSettings::sleepPinnedPath) == 256.
  char oldValue[256] = {};
  if (pinnedPath != nullptr && pinnedPathSize > 0) {
    std::strncpy(oldValue, pinnedPath, sizeof(oldValue) - 1);
  }

  setPinnedPath(pinnedPath, pinnedPathSize, value);
  if (!saveSettings || !saveSettings()) {
    // Restore in-memory state so it stays consistent with what is on disk.
    setPinnedPath(pinnedPath, pinnedPathSize, oldValue);
    return {500, "text/plain", "Failed to save settings"};
  }

  JsonDocument response;
  response["pinnedPath"] = pinnedPath;
  return jsonResponse(response);
}

bool copyCoverToPinnedPath(const std::string& coverPath) {
  {
    SpiBusMutex::Guard guard;
    if (!Storage.exists("/sleep")) {
      Storage.mkdir("/sleep");
    }
  }

  bool copyOk = false;
  {
    SpiBusMutex::Guard guard;
    FsFile src;
    FsFile dst;
    if (Storage.openFileForRead("WEB", coverPath.c_str(), src) &&
        Storage.openFileForWrite("WEB", kPinnedSleepCoverPath, dst)) {
      uint8_t buffer[512];
      bool writeError = false;
      while (const size_t read = src.read(buffer, sizeof(buffer))) {
        if (dst.write(buffer, read) != read) {
          writeError = true;
          break;
        }
      }
      dst.close();
      src.close();
      copyOk = !writeError;
    }
  }

  return copyOk;
}

}  // namespace

SleepCoverHttpResult buildSleepCoverGetResponse(const char* pinnedPath) {
  JsonDocument doc;
  doc["path"] = pinnedPath;

  const std::string path = pinnedPath == nullptr ? "" : std::string(pinnedPath);
  const size_t slash = path.find_last_of('/');
  doc["name"] = (slash == std::string::npos) ? path : path.substr(slash + 1);

  return jsonResponse(doc);
}

SleepCoverHttpResult handleSleepCoverPinRequest(const bool hasBody, const String& body, char* pinnedPath,
                                                const size_t pinnedPathSize,
                                                const SleepCoverResolver& resolveBookCoverPath,
                                                const SleepCoverSaveSettings& saveSettings) {
  if (!hasBody) {
    return {400, "text/plain", "Missing body"};
  }

  JsonDocument request;
  if (deserializeJson(request, body.c_str())) {
    return {400, "text/plain", "Invalid JSON"};
  }

  if (!request["bookPath"].isNull()) {
    const char* bookPathValue = request["bookPath"] | "";
    const String rawBookPath(bookPathValue);
    if (!PathUtils::isValidSdPath(rawBookPath)) {
      return {400, "text/plain", "Invalid bookPath"};
    }

    const String bookPath = PathUtils::normalizePath(rawBookPath);
    std::string coverPath;
    if (!resolveBookCoverPath || !resolveBookCoverPath(bookPath, coverPath) || coverPath.empty()) {
      return {404, "text/plain", "No cover available for this book"};
    }

    if (!copyCoverToPinnedPath(coverPath)) {
      return {500, "text/plain", "Failed to copy cover"};
    }

    return savePinnedPath(pinnedPath, pinnedPathSize, kPinnedSleepCoverPath, saveSettings);
  }

  const char* pathValue = request["path"] | "";
  const String rawPath(pathValue);
  if (rawPath.isEmpty()) {
    char oldValue[256] = {};
    if (pinnedPath != nullptr && pinnedPathSize > 0) {
      std::strncpy(oldValue, pinnedPath, sizeof(oldValue) - 1);
    }
    setPinnedPath(pinnedPath, pinnedPathSize, "");
    if (!saveSettings || !saveSettings()) {
      setPinnedPath(pinnedPath, pinnedPathSize, oldValue);
      return {500, "text/plain", "Failed to save"};
    }
    return {200, "text/plain", "Cleared"};
  }

  if (!PathUtils::isValidSdPath(rawPath)) {
    return {400, "text/plain", "Invalid path"};
  }

  const String normalizedPath = PathUtils::normalizePath(rawPath);
  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(normalizedPath.c_str());
  }
  if (!exists) {
    return {404, "text/plain", "File not found"};
  }

  return savePinnedPath(pinnedPath, pinnedPathSize, normalizedPath.c_str(), saveSettings);
}

}  // namespace network
