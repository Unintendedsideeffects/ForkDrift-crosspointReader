#include "network/BufferedHttpUpload.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <cstring>

#include "SpiBusMutex.h"

namespace network {

namespace {

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

}  // namespace

BufferedHttpUploadSession& sharedBufferedHttpUploadSession() {
  static BufferedHttpUploadSession session;
  return session;
}

bool BufferedHttpUploadSession::flushBuffer(const char* logLabel) {
  if (uploadBufferPos > 0 && uploadFile) {
    SpiBusMutex::Guard guard;
    esp_task_wdt_reset();
    const unsigned long writeStart = millis();
    const size_t written = uploadFile.write(uploadBuffer, uploadBufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    esp_task_wdt_reset();

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

void BufferedHttpUploadSession::handleUpload(WebServer* server, const BufferedHttpUploadConfig& config) {
  if (server == nullptr) {
    return;
  }

  const char* logLabel = (config.logLabel != nullptr) ? config.logLabel : "UPLOAD";
  const char* storageTag = (config.storageTag != nullptr) ? config.storageTag : logLabel;

  esp_task_wdt_reset();

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    esp_task_wdt_reset();

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

    esp_task_wdt_reset();
    {
      SpiBusMutex::Guard guard;
      targetExisted = Storage.exists(targetFilePath);
      Storage.remove(tempFilePath);
      Storage.remove(backupFilePath);
    }
    if (targetExisted) {
      LOG_DBG("WEB", "[%s] Replacing existing file after successful upload: %s", logLabel, targetFilePath);
    }

    esp_task_wdt_reset();
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
    esp_task_wdt_reset();

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

void BufferedHttpUploadSession::reset() {
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

}  // namespace network
