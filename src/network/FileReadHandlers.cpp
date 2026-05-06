#include <Arduino.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointWebServer.h"
#include "network/FileReadApi.h"
#include "util/AgentDebugLog.h"

void CrossPointWebServer::handleFileListData() const {
  noteWebUiAccess();
  const String rawPath = server->hasArg("path") ? server->arg("path") : "";
  // #region agent log
  {
    char data[160];
    snprintf(data, sizeof(data), "{\"heap\":%u,\"rawPathLen\":%u}", static_cast<unsigned int>(ESP.getFreeHeap()),
             static_cast<unsigned int>(rawPath.length()));
    agentDebugLog("initial", "H4,H5,H6", "FileReadHandlers.cpp:handleFileListData", "files API handler entry", data);
  }
  // #endregion

  String normalizedPath;
  size_t entryCount = 0;
  if (!network::streamFileListJson(*server, rawPath, SETTINGS.showHiddenFiles, &normalizedPath, &entryCount)) {
    return;
  }
  // #region agent log
  {
    char data[160];
    snprintf(data, sizeof(data), "{\"heap\":%u,\"entries\":%u}", static_cast<unsigned int>(ESP.getFreeHeap()),
             static_cast<unsigned int>(entryCount));
    agentDebugLog("initial", "H4,H5,H6", "FileReadHandlers.cpp:handleFileListData", "files API handler exit", data);
  }
  // #endregion
  LOG_DBG("WEB", "Served file listing for path: %s", normalizedPath.c_str());
}

void CrossPointWebServer::handleDownload() const {
  requestCount++;
  noteWebUiAccess();
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  const auto result = network::resolveDownload(server->arg("path"));
  if (!result.ok()) {
    server->send(result.statusCode, result.contentType, result.body);
    return;
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(result.normalizedPath.c_str());
  }
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }

  constexpr size_t chunkSize = 8192;
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize));
  if (!buffer) {
    LOG_ERR("WEB", "Download: malloc failed for %u byte buffer", static_cast<unsigned int>(chunkSize));
    {
      SpiBusMutex::Guard guard;
      file.close();
    }
    server->send(500, "text/plain", "Insufficient memory for download");
    return;
  }

  server->setContentLength(result.fileSize);
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
  server->send(200, result.contentType.c_str(), "");

  WiFiClient client = server->client();
  bool downloadOk = true;
  while (downloadOk) {
    // Reset WDT before acquiring the SPI mutex — the display may hold the bus
    // for up to ~2 s during an e-ink refresh, and the mutex uses portMAX_DELAY.
    esp_task_wdt_reset();

    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(buffer, chunkSize);
    }
    if (bytesRead == 0) {
      break;
    }

    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      esp_task_wdt_reset();
      const size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
    }
  }

  free(buffer);

  {
    SpiBusMutex::Guard guard;
    file.close();
  }
}
