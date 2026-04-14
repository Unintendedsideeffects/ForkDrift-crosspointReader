#include "CrossPointWebServer.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include "CrossPointSettings.h"
#include "SpiBusMutex.h"
#include "network/FileReadApi.h"

void CrossPointWebServer::handleFileListData() const {
  const auto result = network::buildFileListJson(server->hasArg("path") ? server->arg("path") : "",
                                                 SETTINGS.showHiddenFiles);
  server->send(result.statusCode, result.contentType, result.body);
  if (result.ok()) {
    LOG_DBG("WEB", "Served file listing page for path: %s", result.normalizedPath.c_str());
  }
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

  server->setContentLength(result.fileSize);
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
  server->send(200, result.contentType.c_str(), "");

  WiFiClient client = server->client();
  constexpr size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];
  bool downloadOk = true;

  while (downloadOk) {
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

  {
    SpiBusMutex::Guard guard;
    file.close();
  }
}
