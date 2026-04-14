#include "CrossPointWebServer.h"

#include <HalStorage.h>
#include <esp_task_wdt.h>

#include <string>

#include "SpiBusMutex.h"
#include "core/features/FeatureModules.h"
#include "network/AssetReadApi.h"
#include "util/RecentBooksStore.h"

void CrossPointWebServer::handleCover() const {
  const auto result = network::resolveCoverAssetPath(server->arg("path"), RECENT_BOOKS.getBooks(),
                                                     [](const String& bookPath, std::string& coverPath) {
                                                       core::FeatureModules::tryGetDocumentCoverPath(bookPath, coverPath);
                                                       return !coverPath.empty();
                                                     });
  if (!result.ok()) {
    server->send(result.statusCode, result.contentType, result.body);
    return;
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(result.resolvedPath.c_str());
  }
  if (!file) {
    server->send(500, "text/plain", "Failed to open cover");
    return;
  }

  size_t fileSize = 0;
  {
    SpiBusMutex::Guard guard;
    fileSize = file.size();
  }

  server->setContentLength(fileSize);
  server->sendHeader("Cache-Control", "public, max-age=3600");
  server->send(200, "image/bmp", "");

  WiFiClient client = server->client();
  uint8_t buffer[1024];
  bool sendOk = true;
  while (sendOk) {
    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(buffer, sizeof(buffer));
    }
    if (bytesRead == 0) break;
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      const size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        LOG_DBG("WEB", "Asset send: client disconnected after %u bytes", static_cast<unsigned int>(totalWritten));
        sendOk = false;
        break;
      }
      if (wrote < (bytesRead - totalWritten)) {
        LOG_DBG("WEB", "Asset send: partial write %u/%u bytes, retrying", static_cast<unsigned int>(wrote),
                static_cast<unsigned int>(bytesRead - totalWritten));
      }
      totalWritten += wrote;
    }
    yield();
    esp_task_wdt_reset();
  }
  {
    SpiBusMutex::Guard guard;
    file.close();
  }
}

void CrossPointWebServer::handleSleepImages() const {
  server->send(200, "application/json", network::buildSleepImagesJson());
}
