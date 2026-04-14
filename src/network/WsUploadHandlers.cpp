#include "CrossPointWebServer.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include "CrossPointState.h"
#include "SpiBusMutex.h"
#include "network/CacheInvalidation.h"
#include "util/InputValidation.h"
#include "util/PathUtils.h"

namespace {

constexpr size_t WS_CONTROL_MESSAGE_MAX_BYTES = 1024;
constexpr size_t WS_UPLOAD_MAX_BYTES = 512UL * 1024UL * 1024UL;

bool parseStrictSize(const String& token, size_t& outValue) {
  return InputValidation::parseStrictPositiveSize(token.c_str(), token.length(), WS_UPLOAD_MAX_BYTES, outValue);
}

}  // namespace

// Abort any in-progress WebSocket upload: closes the file and removes the partial file from storage.
void CrossPointWebServer::abortWsUpload(const char* tag) {
  {
    SpiBusMutex::Guard guard;
    wsUploadFile.close();
  }
  String filePath = wsUploadPath;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += wsUploadFileName;
  bool removed = false;
  {
    SpiBusMutex::Guard guard;
    removed = Storage.remove(filePath.c_str());
  }
  if (removed) {
    LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
  } else {
    LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
  }
  wsUploadInProgress = false;
  wsUploadClientNum = kNoUploadClient;
  wsLastProgressSent = 0;
}

// WebSocket event handler for fast binary uploads.
//
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  auto buildWsUploadFilePath = [this]() {
    String filePath = wsUploadPath;
    if (!filePath.endsWith("/")) {
      filePath += "/";
    }
    filePath += wsUploadFileName;
    return filePath;
  };

  auto cleanupPartialWsUpload = [this, &buildWsUploadFilePath](const char* reason) {
    const bool hasPath = !wsUploadPath.isEmpty() && !wsUploadFileName.isEmpty();
    const String filePath = hasPath ? buildWsUploadFilePath() : String();
    {
      SpiBusMutex::Guard guard;
      if (wsUploadFile) {
        wsUploadFile.close();
      }
      if (hasPath) {
        Storage.remove(filePath.c_str());
      }
    }
    if (hasPath) {
      LOG_DBG("WS", "Deleted incomplete upload (%s): %s", reason, filePath.c_str());
    }
  };

  auto resetWsUploadState = [this]() {
    wsUploadInProgress = false;
    wsUploadFileName.clear();
    wsUploadPath.clear();
    wsUploadSize = 0;
    wsUploadReceived = 0;
    wsLastProgressSent = 0;
    wsUploadStartTime = 0;
    wsUploadOwnerClient = 0;
    wsUploadOwnerValid = false;
  };

  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      if ((wsUploadInProgress || wsUploadFile) && wsUploadOwnerValid && wsUploadOwnerClient == num) {
        cleanupPartialWsUpload("owner disconnect");
        resetWsUploadState();
      } else if (wsUploadInProgress || wsUploadFile) {
        LOG_DBG("WS", "Ignoring disconnect from non-owner client %u during upload", num);
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      if ((wsUploadInProgress || wsUploadFile) && wsUploadOwnerValid && wsUploadOwnerClient != num) {
        wsServer->sendTXT(num, "ERROR:Upload in progress by another client");
        return;
      }

      if (!payload) {
        wsServer->sendTXT(num, "ERROR:Missing control payload");
        return;
      }
      if (length == 0 || length > WS_CONTROL_MESSAGE_MAX_BYTES) {
        wsServer->sendTXT(num, "ERROR:Control message too large");
        return;
      }

      String msg;
      msg.reserve(length);
      for (size_t i = 0; i < length; i++) {
        const char c = static_cast<char>(payload[i]);
        if (c == '\0') {
          wsServer->sendTXT(num, "ERROR:Invalid control payload");
          return;
        }
        msg += c;
      }
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      // Remote page-turn commands — handled before upload guard so they work during idle.
      if (msg.equalsIgnoreCase("PAGE:NEXT") || msg.equalsIgnoreCase("PAGE:FORWARD")) {
        APP_STATE.pendingPageTurn = 1;
        wsServer->sendTXT(num, "OK");
        return;
      }
      if (msg.equalsIgnoreCase("PAGE:PREV") || msg.equalsIgnoreCase("PAGE:BACK")) {
        APP_STATE.pendingPageTurn = -1;
        wsServer->sendTXT(num, "OK");
        return;
      }

      if (!msg.startsWith("START:")) {
        wsServer->sendTXT(num, "ERROR:Unknown command");
        return;
      }

      // Parse: START:<filename>:<size>:<path> (filename/path URL-encoded)
      const int firstColon = msg.indexOf(':', 6);
      const int secondColon = firstColon > 0 ? msg.indexOf(':', firstColon + 1) : -1;
      if (firstColon <= 0 || secondColon <= 0) {
        wsServer->sendTXT(num, "ERROR:Invalid START format");
        return;
      }

      String requestedFileName = PathUtils::urlDecode(msg.substring(6, firstColon));
      String requestedPath = PathUtils::urlDecode(msg.substring(secondColon + 1));
      size_t requestedSize = 0;
      if (!parseStrictSize(msg.substring(firstColon + 1, secondColon), requestedSize)) {
        wsServer->sendTXT(num, "ERROR:Invalid size (1..512MB)");
        return;
      }
      if (requestedSize > WS_UPLOAD_MAX_BYTES) {
        LOG_WRN("WS", "Rejected upload with declared size %u bytes (max %u)", static_cast<unsigned int>(requestedSize),
                static_cast<unsigned int>(WS_UPLOAD_MAX_BYTES));
        wsServer->sendTXT(num, "ERROR:Declared size exceeds limit");
        return;
      }

      // Validate filename against traversal attacks
      if (!PathUtils::isValidFilename(requestedFileName)) {
        LOG_WRN("WS", "Invalid filename rejected: %s", requestedFileName.c_str());
        wsServer->sendTXT(num, "ERROR:Invalid filename");
        return;
      }
      if (PathUtils::isProtectedWebComponent(requestedFileName)) {
        LOG_WRN("WS", "Protected filename rejected: %s", requestedFileName.c_str());
        wsServer->sendTXT(num, "ERROR:Protected filename");
        return;
      }

      // Validate path against traversal attacks
      if (!PathUtils::isValidSdPath(requestedPath)) {
        LOG_WRN("WS", "Path validation failed: %s", requestedPath.c_str());
        wsServer->sendTXT(num, "ERROR:Invalid path");
        return;
      }

      requestedPath = PathUtils::normalizePath(requestedPath);
      if (PathUtils::pathContainsProtectedItem(requestedPath)) {
        LOG_WRN("WS", "Protected path rejected: %s", requestedPath.c_str());
        wsServer->sendTXT(num, "ERROR:Protected path");
        return;
      }

      if (wsUploadInProgress || wsUploadFile) {
        cleanupPartialWsUpload("superseded by owner");
        resetWsUploadState();
      }

      wsUploadFileName = requestedFileName;
      wsUploadPath = requestedPath;
      wsUploadSize = requestedSize;
      wsUploadReceived = 0;
      wsLastProgressSent = 0;
      wsUploadStartTime = millis();
      wsUploadClientNum = num;
      wsUploadOwnerClient = num;
      wsUploadOwnerValid = true;

      const String filePath = buildWsUploadFilePath();
      LOG_DBG("WS", "Starting upload: %s (%u bytes) to %s", wsUploadFileName.c_str(),
              static_cast<unsigned int>(wsUploadSize), filePath.c_str());

      // Check if file exists and remove it
      esp_task_wdt_reset();
      {
        SpiBusMutex::Guard guard;
        if (Storage.exists(filePath.c_str())) {
          Storage.remove(filePath.c_str());
        }
      }

      // Open file for writing
      esp_task_wdt_reset();
      bool fileOpened = false;
      {
        SpiBusMutex::Guard guard;
        fileOpened = Storage.openFileForWrite("WS", filePath, wsUploadFile);
      }
      if (!fileOpened) {
        wsServer->sendTXT(num, "ERROR:Failed to create file");
        resetWsUploadState();
        return;
      }
      esp_task_wdt_reset();

      // Zero-byte upload: complete immediately without waiting for BIN frames
      if (wsUploadSize == 0) {
        wsUploadFile.close();
        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = 0;
        wsLastCompleteAt = millis();
        LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
        invalidateFeatureCachesIfNeeded(filePath);
        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
        wsUploadOwnerClient = 0;
        wsUploadOwnerValid = false;
        break;
      }

      wsUploadInProgress = true;
      wsServer->sendTXT(num, "READY");
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }
      if (!wsUploadOwnerValid || wsUploadOwnerClient != num) {
        wsServer->sendTXT(num, "ERROR:Upload in progress by another client");
        return;
      }
      if (!payload) {
        cleanupPartialWsUpload("missing chunk payload");
        resetWsUploadState();
        wsServer->sendTXT(num, "ERROR:Missing chunk payload");
        return;
      }
      if (length == 0) {
        wsServer->sendTXT(num, "ERROR:Empty chunk");
        return;
      }
      if (wsUploadReceived > wsUploadSize || length > (wsUploadSize - wsUploadReceived)) {
        cleanupPartialWsUpload("oversize chunk");
        resetWsUploadState();
        wsServer->sendTXT(num, "ERROR:Chunk exceeds declared size");
        return;
      }

      // Write binary data directly to file
      esp_task_wdt_reset();
      size_t written = 0;
      {
        SpiBusMutex::Guard guard;
        written = wsUploadFile.write(payload, length);
      }
      esp_task_wdt_reset();

      if (written != length) {
        cleanupPartialWsUpload("write failure");
        resetWsUploadState();
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived == wsUploadSize) {
        {
          SpiBusMutex::Guard guard;
          wsUploadFile.close();
        }
        wsUploadInProgress = false;
        wsUploadClientNum = kNoUploadClient;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%u bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(),
                static_cast<unsigned int>(wsUploadSize), elapsed, kbps);

        // Clear caches to prevent stale data when overwriting files
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        invalidateFeatureCachesIfNeeded(filePath);

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
        wsUploadOwnerClient = 0;
        wsUploadOwnerValid = false;
      }
      break;
    }

    default:
      break;
  }
}
