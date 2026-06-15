#include <Arduino.h>
#include <Logging.h>

#include "CrossPointWebServer.h"
#include "network/SettingsApi.h"

namespace {
// POST rebuilds the full std::vector<SettingInfo> (getSettingsList) to apply
// values — keep in sync with kMinHeapForSettingsRebuild in SettingsActivity.cpp.
constexpr uint32_t kMinHeapForSettingsList = 48000;

// GET streams settings one at a time (no full vector, no cumulative String — see
// network::streamSettingsListJson), so it needs far less headroom. Peak transient
// is the guarded font/sleep prelude plus a single SettingInfo + one JsonDocument
// + a ~768 B stack buffer. ~16 KB keeps a ~3x margin over that while letting the
// settings page load with a book (or the background server) still resident — the
// exact low-heap state that used to 503 against the old 48 KB guard.
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
  // applySettingsJson builds the full settings list (getSettingsList) — the
  // same allocation burst the GET guard above protects against. Without this
  // check a low-heap POST dies in a throwing vector allocation (bad_alloc ->
  // terminate -> abort) instead of returning an error.
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForSettingsList) {
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
