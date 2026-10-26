#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <string>
#include <vector>

#include "SpiBusMutex.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "network/server/AssetReadApi.h"
#include "network/server/CrossPointWebServer.h"
#include "util/CoverThumbSizes.h"
#include "util/RecentBooksStore.h"

namespace {

// Concrete names for a "thumb_[HEIGHT].bmp" template, largest thumb first so the
// app and web UI get the sharpest one that exists. Must match the names
// UITheme::getCoverThumbPath() generates.
std::vector<std::string> coverThumbSuffixesLargestFirst() {
  coverthumbs::Size sizes[8] = {};
  int count = coverthumbs::all(sizes, 8);
  coverthumbs::appendUnique(sizes, 8, count, {0, UITheme::getInstance().getMetrics().homeCoverHeight});
  std::sort(sizes, sizes + count,
            [](const coverthumbs::Size& a, const coverthumbs::Size& b) { return a.height > b.height; });
  std::vector<std::string> suffixes;
  suffixes.reserve(count);
  for (int i = 0; i < count; ++i) {
    suffixes.push_back(sizes[i].width > 0
                           ? "W" + std::to_string(sizes[i].width) + "_H" + std::to_string(sizes[i].height)
                           : std::to_string(sizes[i].height));
  }
  return suffixes;
}

}  // namespace

void CrossPointWebServer::handleCover() const {
  const auto result = network::resolveCoverAssetPath(
      server->arg("path"), RECENT_BOOKS.getBooksSnapshot(),
      [](const String& bookPath, std::string& coverPath) {
        core::FeatureModules::tryGetDocumentCoverPath(bookPath, coverPath);
        return !coverPath.empty();
      },
      coverThumbSuffixesLargestFirst());
  if (!result.ok()) {
    server->send(result.statusCode, result.contentType, result.body);
    return;
  }

  HalFile file;
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
