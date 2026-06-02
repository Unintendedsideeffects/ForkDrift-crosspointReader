#include "features/trmnl_switch/Registration.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>

#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "core/features/FeatureCatalog.h"
#include "core/features/FeatureModules.h"
#include "core/registries/LifecycleRegistry.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/HttpDownloader.h"
#include "network/WebUtils.h"
#include "network/html/TerminusPluginPageHtml.generated.h"
#include "util/TerminusCredentialStore.h"

namespace features::trmnl_switch {

#if ENABLE_TRMNL_SWITCH
namespace {

static constexpr const char* TRMNL_DEST_PATH = "/sleep/trmnl_latest.bmp";
static constexpr const char* TRMNL_DEFAULT_BASE = "https://api.trmnl.com";

// Poll /api/display, get image_url, download, pin as next sleep screen.
static bool fetchAndPinTrmnlImage() {
  if (!TERMINUS_STORE.hasCredentials()) {
    LOG_INF("TRMNL", "No Terminus credentials configured");
    return false;
  }

  const std::string& rawBase = TERMINUS_STORE.baseUrl();
  const std::string base = rawBase.empty() ? std::string(TRMNL_DEFAULT_BASE) : rawBase;
  const std::string displayUrl = base + "/api/display";

  LOG_INF("TRMNL", "Polling %s (id=%s model=%s)", displayUrl.c_str(), TERMINUS_STORE.deviceId().c_str(),
          TERMINUS_STORE.deviceModel().c_str());

  std::string manifest;
  {
    auto* raw = new (std::nothrow) WiFiClientSecure();
    if (!raw) {
      LOG_ERR("TRMNL", "OOM: WiFiClientSecure");
      return false;
    }
    std::unique_ptr<WiFiClientSecure> client(raw);
    client->setInsecure();

    HTTPClient http;
    http.begin(*client, displayUrl.c_str());
    http.addHeader("ID", TERMINUS_STORE.deviceId().c_str());
    http.addHeader("Access-Token", TERMINUS_STORE.apiKey().c_str());
    http.addHeader("Device-Model", TERMINUS_STORE.deviceModel().c_str());
    http.addHeader("Firmware-Version", CROSSPOINT_VERSION);
    http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    const int code = http.GET();
    if (code == HTTP_CODE_OK) {
      manifest = http.getString().c_str();
      LOG_INF("TRMNL", "Display manifest received (%zu bytes)", manifest.size());
    } else {
      LOG_ERR("TRMNL", "Display poll failed: HTTP %d", code);
    }
    http.end();
  }  // client destroyed here

  if (manifest.empty()) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, manifest)) {
    LOG_ERR("TRMNL", "Failed to parse display manifest");
    return false;
  }

  const char* imageUrl = doc["image_url"] | "";
  if (imageUrl[0] == '\0') {
    LOG_ERR("TRMNL", "No image_url in display manifest");
    return false;
  }

  LOG_INF("TRMNL", "Downloading image: %s", imageUrl);
  const auto err = HttpDownloader::downloadToFile(std::string(imageUrl), TRMNL_DEST_PATH);
  if (err != HttpDownloader::OK) {
    LOG_ERR("TRMNL", "Image download failed (err=%d)", static_cast<int>(err));
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
