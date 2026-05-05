#include "network/UploadApi.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "SpiBusMutex.h"
#include "core/features/FeatureModules.h"
#include "network/CacheInvalidation.h"
#include "util/PathUtils.h"

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#define CROSSPOINT_HAS_TASK_WDT 1
#else
#define CROSSPOINT_HAS_TASK_WDT 0
#endif

namespace {

using UploadResolveTarget = bool (*)(WebServer*, const char*, char*, size_t, char*, size_t, char*, size_t);

bool appendUploadSuffix(const char* basePath, const char* suffix, char* out, size_t outSize) {
  if (basePath == nullptr || suffix == nullptr || out == nullptr || outSize == 0) {
    return false;
  }

  const int written = snprintf(out, outSize, "%s%s", basePath, suffix);
  return written >= 0 && static_cast<size_t>(written) < outSize;
}

void cleanupUploadArtifact(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }

  SpiBusMutex::Guard guard;
  Storage.remove(path);
}

bool finalizeUploadReplace(const char* targetPath, const char* tempPath, const char* backupPath,
                           const bool hadExistingFile) {
  if (targetPath == nullptr || tempPath == nullptr || targetPath[0] == '\0' || tempPath[0] == '\0') {
    return false;
  }

  SpiBusMutex::Guard guard;
  bool movedOriginalToBackup = false;

  if (hadExistingFile) {
    if (backupPath == nullptr || backupPath[0] == '\0') {
      return false;
    }
    Storage.remove(backupPath);
    if (!Storage.rename(targetPath, backupPath)) {
      return false;
    }
    movedOriginalToBackup = true;
  }

  if (!Storage.rename(tempPath, targetPath)) {
    if (movedOriginalToBackup) {
      Storage.rename(backupPath, targetPath);
    }
    Storage.remove(tempPath);
    return false;
  }

  if (movedOriginalToBackup) {
    Storage.remove(backupPath);
  }

  return true;
}

struct UploadConfig {
  const char* logLabel = "UPLOAD";
  const char* storageTag = nullptr;
  const char* createFileError = "Failed to create file on SD card";
  const char* chunkWriteError = "Failed to write upload data";
  const char* finalWriteError = "Failed to write final upload data";
  const char* abortedError = "Upload aborted";
  bool logProgress = false;
  UploadResolveTarget resolveTarget = nullptr;
};

inline void resetUploadWatchdog() {
#if CROSSPOINT_HAS_TASK_WDT
  esp_task_wdt_reset();
#endif
}

class UploadSession {
 public:
  static constexpr size_t kMaxFileNameLen = 256;
  static constexpr size_t kMaxUploadPathLen = 256;
  static constexpr size_t kMaxTargetFilePathLen = 512;
  static constexpr size_t kMaxWorkingFilePathLen = kMaxTargetFilePathLen + 16;
  static constexpr size_t kMaxErrorLen = 128;

  void handleUpload(WebServer* server, const UploadConfig& config);
  void reset();

  bool succeeded() const { return uploadSuccess; }
  const char* fileName() const { return uploadFileName; }
  const char* uploadPath() const { return uploadPathValue; }
  const char* filePath() const { return targetFilePath; }
  const char* error() const { return uploadError; }
  size_t size() const { return uploadSize; }

 private:
  bool flushBuffer(const char* logLabel);

  FsFile uploadFile;
  char uploadFileName[kMaxFileNameLen] = {};
  char uploadPathValue[kMaxUploadPathLen] = "/";
  char targetFilePath[kMaxTargetFilePathLen] = {};
  char tempFilePath[kMaxWorkingFilePathLen] = {};
  char backupFilePath[kMaxWorkingFilePathLen] = {};
  size_t uploadSize = 0;
  bool uploadSuccess = false;
  bool targetExisted = false;
  char uploadError[kMaxErrorLen] = {};
  size_t uploadBufferPos = 0;
  unsigned long uploadStartTime = 0;
  unsigned long totalWriteTime = 0;
  size_t writeCount = 0;
  size_t uploadLastLoggedSize = 0;
  static constexpr size_t kBufferSize = 4096;
  uint8_t uploadBuffer[kBufferSize] = {};
};

UploadSession& uploadSession() {
  static UploadSession session;
  return session;
}

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

const UploadConfig kWebUploadConfig = {"UPLOAD",
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
  if (lowerPath == "/sleep.bmp" || lowerPath == "/sleep.png" || lowerPath == "/sleep.jpg" ||
      lowerPath == "/sleep.jpeg" || lowerPath.startsWith("/sleep/") || lowerPath == "/sleep") {
    invalidateSleepImageCache();
  }
}

network::UploadPostResult buildUploadPostResultImpl() {
  network::UploadPostResult result;
  if (uploadSession().succeeded()) {
    invalidateUploadCachesIfNeeded(uploadSession().filePath());
    core::FeatureModules::onUploadCompleted(uploadSession().uploadPath(), uploadSession().fileName());
    result.statusCode = 200;
    result.body = String("File uploaded successfully: ") + uploadSession().fileName();
  } else {
    const char* uploadError = uploadSession().error();
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

  uploadSession().handleUpload(server, kWebUploadConfig);
}

UploadPostResult buildUploadPostResult() { return buildUploadPostResultImpl(); }
void resetUploadSession() { uploadSession().reset(); }

}  // namespace network

// The full upload implementation requires Arduino-compatible HTTP upload types
// (HTTPUpload, UPLOAD_FILE_START, etc.). The host shim provides compatible
// definitions so the same upload state machine can run in host builds.
#if defined(ARDUINO) || defined(CROSSPOINT_HOST_BUILD)

bool UploadSession::flushBuffer(const char* logLabel) {
  if (uploadBufferPos > 0 && uploadFile) {
    SpiBusMutex::Guard guard;
    resetUploadWatchdog();
    const unsigned long writeStart = millis();
    const size_t written = uploadFile.write(uploadBuffer, uploadBufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetUploadWatchdog();

    if (written != uploadBufferPos) {
      LOG_DBG("WEB", "[%s] Buffer flush failed: expected %u, wrote %u", logLabel,
              static_cast<unsigned int>(uploadBufferPos), static_cast<unsigned int>(written));
      uploadBufferPos = 0;
      return false;
    }

    uploadBufferPos = 0;
  }

  return true;
}

void UploadSession::handleUpload(WebServer* server, const UploadConfig& config) {
  if (server == nullptr) {
    return;
  }

  const char* logLabel = (config.logLabel != nullptr) ? config.logLabel : "UPLOAD";
  const char* storageTag = (config.storageTag != nullptr) ? config.storageTag : logLabel;

  resetUploadWatchdog();

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    resetUploadWatchdog();

    snprintf(uploadFileName, sizeof(uploadFileName), "%s", upload.filename.c_str());
    uploadPathValue[0] = '/';
    uploadPathValue[1] = '\0';
    targetFilePath[0] = '\0';
    tempFilePath[0] = '\0';
    backupFilePath[0] = '\0';
    uploadSize = 0;
    uploadSuccess = false;
    targetExisted = false;
    uploadError[0] = '\0';
    uploadBufferPos = 0;
    uploadStartTime = millis();
    totalWriteTime = 0;
    writeCount = 0;
    uploadLastLoggedSize = 0;

    if (config.resolveTarget == nullptr ||
        !config.resolveTarget(server, uploadFileName, uploadPathValue, sizeof(uploadPathValue), targetFilePath,
                              sizeof(targetFilePath), uploadError, sizeof(uploadError))) {
      return;
    }

    if (uploadPathValue[0] == '\0') {
      uploadPathValue[0] = '/';
      uploadPathValue[1] = '\0';
    }
    if (targetFilePath[0] == '\0') {
      snprintf(uploadError, sizeof(uploadError), "Missing upload target");
      return;
    }
    if (!appendUploadSuffix(targetFilePath, ".part", tempFilePath, sizeof(tempFilePath)) ||
        !appendUploadSuffix(targetFilePath, ".bak", backupFilePath, sizeof(backupFilePath))) {
      snprintf(uploadError, sizeof(uploadError), "Path too long");
      return;
    }

    LOG_DBG("WEB", "[%s] START: %s to path: %s", logLabel, uploadFileName, uploadPathValue);
    LOG_DBG("WEB", "[%s] Free heap: %d bytes", logLabel, ESP.getFreeHeap());

    resetUploadWatchdog();
    {
      SpiBusMutex::Guard guard;
      targetExisted = Storage.exists(targetFilePath);
      Storage.remove(tempFilePath);
      Storage.remove(backupFilePath);
    }
    if (targetExisted) {
      LOG_DBG("WEB", "[%s] Replacing existing file after successful upload: %s", logLabel, targetFilePath);
    }

    resetUploadWatchdog();
    bool opened = false;
    {
      SpiBusMutex::Guard guard;
      opened = Storage.openFileForWrite(storageTag, tempFilePath, uploadFile);
    }
    if (!opened) {
      snprintf(uploadError, sizeof(uploadError), "%s", config.createFileError);
      LOG_DBG("WEB", "[%s] FAILED to create temp file: %s", logLabel, tempFilePath);
      return;
    }
    resetUploadWatchdog();

    LOG_DBG("WEB", "[%s] Temp file created successfully: %s", logLabel, tempFilePath);
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadError[0] == '\0') {
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = kBufferSize - uploadBufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(uploadBuffer + uploadBufferPos, data, toCopy);
        uploadBufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        if (uploadBufferPos >= kBufferSize) {
          if (!flushBuffer(logLabel)) {
            snprintf(uploadError, sizeof(uploadError), "%s", config.chunkWriteError);
            {
              SpiBusMutex::Guard guard;
              uploadFile.close();
            }
            cleanupUploadArtifact(tempFilePath);
            return;
          }
        }
      }

      uploadSize += upload.currentSize;

#if LOG_LEVEL >= 2
      if (config.logProgress && uploadSize - uploadLastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (uploadSize / 1024.0F) / (elapsed / 1000.0F) : 0.0F;
        LOG_DBG("WEB", "[%s] %u bytes (%.1f KB), %.1f KB/s, %u writes", logLabel, static_cast<unsigned int>(uploadSize),
                uploadSize / 1024.0F, kbps, static_cast<unsigned int>(writeCount));
        uploadLastLoggedSize = uploadSize;
      }
#endif
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      if (!flushBuffer(logLabel)) {
        snprintf(uploadError, sizeof(uploadError), "%s", config.finalWriteError);
      }
      {
        SpiBusMutex::Guard guard;
        uploadFile.close();
      }

      if (uploadError[0] == '\0') {
        if (!finalizeUploadReplace(targetFilePath, tempFilePath, backupFilePath, targetExisted)) {
          snprintf(uploadError, sizeof(uploadError), "Failed to finalize upload");
        } else {
          uploadSuccess = true;
        }
      }

      if (!uploadSuccess) {
        cleanupUploadArtifact(tempFilePath);
        cleanupUploadArtifact(backupFilePath);
      }

      if (uploadSuccess) {
#if LOG_LEVEL >= 2
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (uploadSize / 1024.0F) / (elapsed / 1000.0F) : 0.0F;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0F / elapsed) : 0.0F;
        LOG_DBG("WEB", "[%s] Complete: %s (%u bytes in %lu ms, avg %.1f KB/s)", logLabel, uploadFileName,
                static_cast<unsigned int>(uploadSize), elapsed, avgKbps);
        LOG_DBG("WEB", "[%s] Diagnostics: %u writes, total write time: %lu ms (%.1f%%)", logLabel,
                static_cast<unsigned int>(writeCount), totalWriteTime, writePercent);
#endif
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadBufferPos = 0;
    if (uploadFile) {
      SpiBusMutex::Guard guard;
      uploadFile.close();
    }
    cleanupUploadArtifact(tempFilePath);
    cleanupUploadArtifact(backupFilePath);
    snprintf(uploadError, sizeof(uploadError), "%s", config.abortedError);
    LOG_DBG("WEB", "[%s] Upload aborted", logLabel);
  }
}

#else

bool UploadSession::flushBuffer(const char* /*logLabel*/) {
  uploadBufferPos = 0;
  return true;
}

void UploadSession::handleUpload(WebServer* /*server*/, const UploadConfig& /*config*/) {}

#endif

void UploadSession::reset() {
  if (uploadFile) {
    SpiBusMutex::Guard guard;
    uploadFile.close();
  }

  uploadFileName[0] = '\0';
  uploadPathValue[0] = '/';
  uploadPathValue[1] = '\0';
  targetFilePath[0] = '\0';
  tempFilePath[0] = '\0';
  backupFilePath[0] = '\0';
  uploadSize = 0;
  uploadSuccess = false;
  targetExisted = false;
  uploadError[0] = '\0';
  uploadBufferPos = 0;
  uploadStartTime = 0;
  totalWriteTime = 0;
  writeCount = 0;
  uploadLastLoggedSize = 0;
}
