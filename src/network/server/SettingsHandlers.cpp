#include <Arduino.h>
#include <Logging.h>

#include "core/registries/HeapReclaimRegistry.h"
#include "network/server/CrossPointWebServer.h"
#include "network/server/SettingsApi.h"

namespace {
constexpr uint32_t kMinHeapForSettingsApply = 16000;
constexpr uint32_t kMinHeapForSettingsStream = 16000;
}  // namespace

void CrossPointWebServer::handleGetSettings() const {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForSettingsStream) {
    LOG_WRN("WEB", "Settings GET rejected: %u bytes free < %u required (%u short)", freeHeap, kMinHeapForSettingsStream,
            kMinHeapForSettingsStream - freeHeap);
    server->send(503, "application/json", "{\"error\":\"low memory\"}");
    return;
  }
  LOG_DBG("WEB", "Settings GET: streaming with %u bytes free", freeHeap);

  // Chunked transfer: send headers with an unknown content length, then push each
  // JSON fragment as its own chunk. Never buffers more than one setting's JSON.
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  network::streamSettingsListJson(
      [](void* ctx, const char* chunk) { static_cast<WebServer*>(ctx)->sendContent(String(chunk)); }, server.get());
  server->sendContent("");  // terminating 0-length chunk closes the response
}

void CrossPointWebServer::handlePostSettings() {
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForSettingsApply && !core::HeapReclaimRegistry::empty()) {
    core::HeapReclaimRegistry::releaseAll();
    freeHeap = ESP.getFreeHeap();
  }
  if (freeHeap < kMinHeapForSettingsApply) {
    LOG_WRN("WEB", "Settings POST rejected at low heap: %u bytes free", freeHeap);
    server->send(503, "application/json", "{\"error\":\"low memory\"}");
    return;
  }

  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const auto result = network::applySettingsJson(server->arg("plain"));
  if (result.statusCode == 500) {
    LOG_WRN("WEB", "Failed to persist settings to SD card");
  } else if (result.ok()) {
    LOG_DBG("WEB", "Applied %d setting(s)", result.appliedCount);
  }

  server->send(result.statusCode, result.contentType, result.body);
}
