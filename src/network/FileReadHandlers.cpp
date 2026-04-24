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

  // Batch JSON entries into a heap-allocated buffer to reduce TCP segment count.
  // Without batching, each entry causes two sendContent() calls (comma + JSON),
  // each flushing a TCP segment. With batching, ~N/20 segments replace 2*N.
  constexpr size_t kBatchCapacity = 2048;
  constexpr size_t kJsonMax = 512;
  char* const batch = static_cast<char*>(malloc(kBatchCapacity));
  if (!batch) {
    LOG_ERR("WEB", "File list: failed to malloc %u byte batch buffer", (unsigned)kBatchCapacity);
    server->send(500, "text/plain", "Insufficient memory");
    return;
  }
  size_t batchLen = 0;

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  bool seenFirst = false;
  char jsonBuf[kJsonMax];
  JsonDocument doc;

  auto flushBatch = [&]() {
    if (batchLen > 0) {
      server->sendContent(batch, batchLen);
      batchLen = 0;
    }
  };

  network::scanDirectory(pathResult.normalizedPath.c_str(), SETTINGS.showHiddenFiles,
                         [&](const network::DirEntry& entry) {
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

                           // +1 for leading comma if not first entry
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
                         });

  flushBatch();
  free(batch);

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
