#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include "CrossPointSettings.h"
#include "CrossPointWebServer.h"
#include "SpiBusMutex.h"
#include "network/FileListApi.h"
#include "network/FileReadApi.h"

void CrossPointWebServer::handleFileListData() const {
  const String rawPath = server->hasArg("path") ? server->arg("path") : "";
  const auto pathResult = network::resolveFileListPath(rawPath);
  if (!pathResult.ok()) {
    server->send(pathResult.statusCode, pathResult.contentType, pathResult.body);
    return;
  }

  // Stream the JSON array entry-by-entry to avoid building the full response
  // in a single heap-allocated String (large directories can cause OOM).
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  bool seenFirst = false;
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  network::scanDirectory(pathResult.normalizedPath.c_str(), SETTINGS.showHiddenFiles,
                         [&](const network::DirEntry& entry) {
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
                             server->sendContent(",");
                           } else {
                             seenFirst = true;
                           }
                           server->sendContent(output);
                         });

  server->sendContent("]");
  LOG_DBG("WEB", "Served file listing for path: %s", pathResult.normalizedPath.c_str());
}

void CrossPointWebServer::handleDownload() const {
  requestCount++;
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
    SpiBusMutex::Guard guard;
    file.close();
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
