#include "features/trmnl_switch/Registration.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Stream.h>
#include <WebServer.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "SpiBusMutex.h"
#include "core/features/FeatureCatalog.h"
#include "core/features/FeatureModules.h"
#include "core/registries/LifecycleRegistry.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/WebUtils.h"
#include "network/html/TerminusPluginPageHtml.generated.h"
#include "util/TerminusCredentialStore.h"
#include "util/TimeSync.h"
#include "util/UrlUtils.h"

namespace features::trmnl_switch {

#if ENABLE_TRMNL_SWITCH
namespace {

static constexpr const char* TRMNL_DEST_PATH = "/sleep/trmnl_latest.bmp";
static constexpr const char* TRMNL_DEFAULT_BASE = "https://api.trmnl.com";
static constexpr size_t TRMNL_MAX_MANIFEST_BYTES = 16u * 1024u;
static constexpr int TRMNL_HTTP_BUFFER_BYTES = 2048;

extern "C" esp_err_t arduino_esp_crt_bundle_attach(void* conf);

class BoundedManifestSink final : public Stream {
 public:
  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    const size_t room = (body_.size() < TRMNL_MAX_MANIFEST_BYTES) ? TRMNL_MAX_MANIFEST_BYTES - body_.size() : 0;
    const size_t take = size < room ? size : room;
    if (take > 0) {
      body_.append(reinterpret_cast<const char*>(buffer), take);
    }
    if (take < size) {
      overflowed_ = true;
    }
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  bool overflowed() const { return overflowed_; }
  std::string& body() { return body_; }

 private:
  std::string body_;
  bool overflowed_ = false;
};

struct VerifiedFileSink {
  const char* path = nullptr;
  HalFile file;
  size_t bytes = 0;
  bool opened = false;
  bool writeFailed = false;
};

static bool isAllowedRemoteUrl(const std::string& url) { return UrlUtils::isHttpsUrl(url); }

static esp_err_t manifestEventHandler(esp_http_client_event_t* evt) {
  auto* sink = static_cast<BoundedManifestSink*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && sink) {
    sink->write(static_cast<const uint8_t*>(evt->data), static_cast<size_t>(evt->data_len));
    if (sink->overflowed()) {
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static esp_err_t imageEventHandler(esp_http_client_event_t* evt) {
  auto* sink = static_cast<VerifiedFileSink*>(evt->user_data);
  if (evt->event_id != HTTP_EVENT_ON_DATA || !sink || !sink->opened) {
    return ESP_OK;
  }

  auto guard = SpiBusMutex::lock();
  const size_t written =
      sink->file.write(reinterpret_cast<const uint8_t*>(evt->data), static_cast<size_t>(evt->data_len));
  sink->bytes += written;
  if (written != static_cast<size_t>(evt->data_len)) {
    sink->writeFailed = true;
    return ESP_FAIL;
  }
  return ESP_OK;
}

static bool downloadVerifiedImage(const std::string& url, const char* path) {
  if (!isAllowedRemoteUrl(url)) {
    return false;
  }

  Storage.ensureDirectoryExists("/sleep");
  if (Storage.exists(path)) {
    Storage.remove(path);
  }

  VerifiedFileSink sink{};
  sink.path = path;
  if (!Storage.openFileForWrite("TRMNL", path, sink.file, false)) {
    LOG_ERR("TRMNL", "Failed to open %s for image download", path);
    return false;
  }
  sink.opened = true;

  TimeSync::ensureTrustedClock();

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.event_handler = imageEventHandler;
  config.user_data = &sink;
  config.timeout_ms = 30000;
  config.buffer_size = TRMNL_HTTP_BUFFER_BYTES;
  config.buffer_size_tx = TRMNL_HTTP_BUFFER_BYTES;
  config.crt_bundle_attach = arduino_esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    sink.file.close();
    Storage.remove(path);
    LOG_ERR("TRMNL", "Failed to create image HTTP client");
    return false;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  const esp_err_t err = esp_http_client_perform(client);
  const int code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  sink.file.close();
  if (err != ESP_OK || code != 200 || sink.writeFailed || sink.bytes == 0) {
    Storage.remove(path);
    LOG_ERR("TRMNL", "Image download failed: HTTP %d err=%d written=%zu failed=%d", code, err, sink.bytes,
            sink.writeFailed ? 1 : 0);
    return false;
  }

  LOG_INF("TRMNL", "Image downloaded (%zu bytes)", sink.bytes);
  return true;
}

// Poll /api/display, get image_url, download, pin as next sleep screen.
static bool fetchAndPinTrmnlImage() {
  if (!TERMINUS_STORE.hasCredentials()) {
    LOG_INF("TRMNL", "No Terminus credentials configured");
    return false;
  }

  const std::string& rawBase = TERMINUS_STORE.baseUrl();
  const std::string base = rawBase.empty() ? std::string(TRMNL_DEFAULT_BASE) : rawBase;
  if (!isAllowedRemoteUrl(base)) {
    LOG_ERR("TRMNL", "Refusing non-HTTPS base URL: %s", base.c_str());
    return false;
  }
  const std::string displayUrl = base + "/api/display";

  LOG_INF("TRMNL", "Polling %s (model=%s)", displayUrl.c_str(), TERMINUS_STORE.deviceModel().c_str());

  TimeSync::ensureTrustedClock();

  std::string manifest;
  {
    BoundedManifestSink sink;
    esp_http_client_config_t config = {};
    config.url = displayUrl.c_str();
    config.event_handler = manifestEventHandler;
    config.user_data = &sink;
    config.timeout_ms = 10000;
    config.buffer_size = TRMNL_HTTP_BUFFER_BYTES;
    config.buffer_size_tx = TRMNL_HTTP_BUFFER_BYTES;
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("TRMNL", "Failed to create HTTP client");
      return false;
    }

    esp_http_client_set_header(client, "ID", TERMINUS_STORE.deviceId().c_str());
    esp_http_client_set_header(client, "Access-Token", TERMINUS_STORE.apiKey().c_str());
    esp_http_client_set_header(client, "Device-Model", TERMINUS_STORE.deviceModel().c_str());
    esp_http_client_set_header(client, "Firmware-Version", CROSSPOINT_VERSION);
    esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

    const esp_err_t err = esp_http_client_perform(client);
    const int code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && code == 200 && !sink.overflowed()) {
      manifest = std::move(sink.body());
      LOG_INF("TRMNL", "Display manifest received (%zu bytes)", manifest.size());
    } else {
      LOG_ERR("TRMNL", "Display poll failed: HTTP %d err=%d overflow=%d", code, err, sink.overflowed() ? 1 : 0);
    }
  }

  if (manifest.empty()) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, manifest)) {
    LOG_ERR("TRMNL", "Failed to parse display manifest");
    return false;
  }

  const char* imageUrl = doc["image_url"] | "";
  if (imageUrl[0] == '\0' || !isAllowedRemoteUrl(imageUrl)) {
    LOG_ERR("TRMNL", "No image_url in display manifest");
    return false;
  }

  LOG_INF("TRMNL", "Downloading image: %s", imageUrl);
  if (!downloadVerifiedImage(std::string(imageUrl), TRMNL_DEST_PATH)) {
    return false;
  }

  strncpy(SETTINGS.sleepPinnedPath, TRMNL_DEST_PATH, sizeof(SETTINGS.sleepPinnedPath) - 1);
  SETTINGS.sleepPinnedPath[sizeof(SETTINGS.sleepPinnedPath) - 1] = '\0';
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  }
  SETTINGS.saveToFile();
  LOG_INF("TRMNL", "Terminus image pinned as next sleep screen");
  return true;
}

static void onStorageReady() { TERMINUS_STORE.load(); }

static void onBackgroundServerStarted() {
  if (!SETTINGS.trmnlSleepEnabled) {
    return;
  }
  fetchAndPinTrmnlImage();
}

static bool shouldRegisterTerminusRoutes() { return core::FeatureCatalog::isEnabled("trmnl_switch"); }

static void mountTerminusRoutes(WebServer* server) {
  server->on("/plugins/terminus", HTTP_GET, [server] {
    sendPrecompressedHtml(server, TerminusPluginPageHtml, TerminusPluginPageHtmlCompressedSize);
    LOG_DBG("WEB", "Served terminus plugin page");
  });

  server->on("/api/terminus/status", HTTP_GET, [server] {
    JsonDocument doc;
    doc["configured"] = TERMINUS_STORE.hasCredentials();
    doc["device_id"] = TERMINUS_STORE.deviceId().c_str();
    doc["device_model"] = TERMINUS_STORE.deviceModel().c_str();
    doc["base_url"] = TERMINUS_STORE.baseUrl().c_str();
    doc["has_api_key"] = !TERMINUS_STORE.apiKey().empty();
    doc["sleep_enabled"] = static_cast<bool>(SETTINGS.trmnlSleepEnabled);
    std::string out;
    serializeJson(doc, out);
    server->send(200, "application/json", out.c_str());
  });

  server->on("/api/terminus/save", HTTP_POST, [server] {
    if (!server->hasArg("plain")) {
      server->send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server->arg("plain"))) {
      server->send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    const char* apiKey = doc["api_key"] | "";
    const char* deviceId = doc["device_id"] | "";
    if (apiKey[0] == '\0' || deviceId[0] == '\0') {
      server->send(400, "application/json", "{\"error\":\"api_key and device_id required\"}");
      return;
    }
    TERMINUS_STORE.setApiKey(apiKey);
    TERMINUS_STORE.setDeviceId(deviceId);

    const char* model = doc["device_model"] | "";
    if (model[0] != '\0') {
      TERMINUS_STORE.setDeviceModel(model);
    }
    const char* url = doc["base_url"] | "";
    if (url[0] != '\0') {
      if (!isAllowedRemoteUrl(url)) {
        server->send(400, "application/json", "{\"error\":\"base_url must be https\"}");
        return;
      }
      TERMINUS_STORE.setBaseUrl(url);
    }
    if (!TERMINUS_STORE.save()) {
      server->send(500, "application/json", "{\"error\":\"save failed\"}");
      return;
    }
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server->on("/api/terminus/test", HTTP_POST, [server] {
    if (!TERMINUS_STORE.hasCredentials()) {
      server->send(400, "application/json", "{\"error\":\"not configured\"}");
      return;
    }
    const bool ok = fetchAndPinTrmnlImage();
    if (ok) {
      server->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Image fetched and pinned\"}");
    } else {
      server->send(502, "application/json", "{\"error\":\"Fetch failed\"}");
    }
  });

  server->on("/api/terminus/clear", HTTP_POST, [server] {
    TERMINUS_STORE.clear();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });
}

}  // namespace
#endif

void registerFeature() {
#if ENABLE_TRMNL_SWITCH
  if (!core::FeatureModules::hasCapability(core::Capability::TrmnlSwitch)) {
    return;
  }

  core::LifecycleEntry entry{};
  entry.onStorageReady = onStorageReady;
  entry.onBackgroundServerStarted = onBackgroundServerStarted;
  core::LifecycleRegistry::add(entry);

  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "terminus_plugin";
  webRouteEntry.shouldRegister = shouldRegisterTerminusRoutes;
  webRouteEntry.mountRoutes = mountTerminusRoutes;
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::trmnl_switch
