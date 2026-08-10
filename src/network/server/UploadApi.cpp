#include "network/server/UploadApi.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "SpiBusMutex.h"
#include "core/features/FeatureModules.h"
#include "network/http/BufferedHttpUpload.h"
#include "network/server/CacheInvalidation.h"
#include "util/PathUtils.h"

namespace {

bool resolveWebUploadTarget(WebServer* server, const char* uploadFileName, char* uploadPath, size_t uploadPathSize,
                            char* filePath, size_t filePathSize, char* error, size_t errorSize) {
  if (server == nullptr) {
    snprintf(error, errorSize, "Upload server unavailable");
    return false;
  }

  if (!PathUtils::isValidFilename(uploadFileName)) {
    snprintf(error, errorSize, "Invalid filename");
    LOG_WRN("WEB", "[UPLOAD] Invalid filename rejected: %s", uploadFileName);
    return false;
  }
  if (PathUtils::isProtectedWebComponent(uploadFileName)) {
    snprintf(error, errorSize, "Cannot upload protected files");
    LOG_WRN("WEB", "[UPLOAD] Protected filename rejected: %s", uploadFileName);
    return false;
  }

  uploadPath[0] = '/';
  uploadPath[1] = '\0';
  if (server->hasArg("path")) {
    if (!PathUtils::urlDecode(server->arg("path").c_str(), uploadPath, uploadPathSize)) {
      snprintf(error, errorSize, "Path too long");
      LOG_WRN("WEB", "[UPLOAD] Path decode exceeded %u bytes", static_cast<unsigned int>(uploadPathSize));
      return false;
    }

    if (!PathUtils::isValidSdPath(uploadPath)) {
      snprintf(error, errorSize, "Invalid path");
      LOG_WRN("WEB", "[UPLOAD] Path validation failed: %s", uploadPath);
      return false;
    }

    if (!PathUtils::normalizePathInPlace(uploadPath, uploadPathSize)) {
      snprintf(error, errorSize, "Path too long");
      LOG_WRN("WEB", "[UPLOAD] Path normalization exceeded %u bytes", static_cast<unsigned int>(uploadPathSize));
      return false;
    }

    if (PathUtils::pathContainsProtectedItem(uploadPath)) {
      snprintf(error, errorSize, "Cannot upload to protected path");
      LOG_WRN("WEB", "[UPLOAD] Protected upload path rejected: %s", uploadPath);
      return false;
    }
  }

  const size_t uploadPathLength = std::strlen(uploadPath);
  const bool endsWithSlash = uploadPathLength > 0 && uploadPath[uploadPathLength - 1] == '/';
  const int written = snprintf(filePath, filePathSize, "%s%s%s", uploadPath, endsWithSlash ? "" : "/", uploadFileName);
  if (written < 0 || static_cast<size_t>(written) >= filePathSize) {
    snprintf(error, errorSize, "Path too long");
    LOG_WRN("WEB", "[UPLOAD] Combined upload path exceeds limit (%d chars)", written);
    return false;
  }
  if (!PathUtils::isValidSdPath(filePath)) {
    snprintf(error, errorSize, "Path too long");
    LOG_WRN("WEB", "[UPLOAD] Combined upload path rejected: %s", filePath);
    return false;
  }
  return true;
}

const network::BufferedHttpUploadConfig kWebUploadConfig = {"UPLOAD",
                                                            "WEB",
                                                            "Failed to create file on SD card",
                                                            "Failed to write to SD card - disk may be full",
                                                            "Failed to write final data to SD card",
                                                            "Upload aborted",
                                                            true,
                                                            resolveWebUploadTarget};

void invalidateUploadCachesIfNeeded(const String& filePath) {
  core::FeatureModules::onWebFileChanged(filePath);

  String lowerPath = filePath;
  lowerPath.toLowerCase();
  if (lowerPath.startsWith("/sleep/") || lowerPath == "/sleep") {
    invalidateSleepImageCache();
  }
}

network::UploadPostResult buildUploadPostResultImpl() {
  network::UploadPostResult result;
  network::BufferedHttpUploadSession& session = network::sharedBufferedHttpUploadSession();
  if (session.succeeded()) {
    invalidateUploadCachesIfNeeded(session.filePath());
    core::FeatureModules::onUploadCompleted(session.uploadPath(), session.fileName());
    result.statusCode = 200;
    result.body = String("File uploaded successfully: ") + session.fileName();
  } else {
    const char* uploadError = session.error();
    result.body = uploadError[0] == '\0' ? "Unknown error during upload" : uploadError;
  }
  return result;
}

}  // namespace

namespace network {

void startUpload(WebServer* server) {
  if (server == nullptr) {
    return;
  }

  sharedBufferedHttpUploadSession().handleUpload(server, kWebUploadConfig);
}

UploadPostResult buildUploadPostResult() { return buildUploadPostResultImpl(); }
void resetUploadSession() { sharedBufferedHttpUploadSession().reset(); }

}  // namespace network
