#include "features/anki/Registration.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <Logging.h>
#include <WebServer.h>

#if ENABLE_ANKI_SUPPORT
#include "activities/AnkiActivity.h"
#endif
#include "CrossPointSettings.h"
#include "core/features/FeatureCatalog.h"
#include "core/registries/HomeActionRegistry.h"
#include "core/registries/LifecycleRegistry.h"
#include "core/registries/WebRouteRegistry.h"
#include "network/html/AnkiPluginPageHtml.generated.h"
#include "network/http/HttpDownloader.h"
#include "network/server/WebUtils.h"
#include "util/AnkiStore.h"

namespace features::anki {
namespace {

#if ENABLE_ANKI_SUPPORT
static bool shouldRegisterAnkiPluginRoute() { return core::FeatureCatalog::isEnabled("anki_support"); }

static void mountAnkiRoutes(WebServer* server) {
  server->on("/plugins/anki", HTTP_GET, [server] {
    sendPrecompressedHtml(server, AnkiPluginPageHtml, AnkiPluginPageHtmlCompressedSize);
    LOG_DBG("WEB", "Served anki plugin page");
  });
  server->on("/api/anki/cards", HTTP_GET, [server] {
    // buildCardsJson holds the AnkiStore mutex internally; release before send().
    std::string json;
    util::AnkiStore::getInstance().buildCardsJson(json);
    server->send(200, "application/json", json.c_str());
  });
  server->on("/api/anki/clear", HTTP_POST, [server] {
    util::AnkiStore::getInstance().clear();
    util::AnkiStore::getInstance().save();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server->on("/api/anki/delete", HTTP_POST, [server] {
    if (!server->hasArg("plain")) {
      server->send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server->arg("plain"))) {
      server->send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    const int index = doc["index"] | -1;
    if (index < 0) {
      server->send(400, "application/json", "{\"error\":\"missing index\"}");
      return;
    }
    auto& store = util::AnkiStore::getInstance();
    if (static_cast<size_t>(index) >= store.count()) {
      server->send(404, "application/json", "{\"error\":\"out of range\"}");
      return;
    }
    store.removeCard(static_cast<size_t>(index));
    store.save();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server->on("/api/anki/update", HTTP_POST, [server] {
    if (!server->hasArg("plain")) {
      server->send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server->arg("plain"))) {
      server->send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    const int index = doc["index"] | -1;
    if (index < 0 || !doc["back"].is<const char*>()) {
      server->send(400, "application/json", "{\"error\":\"missing index or back\"}");
      return;
    }
    auto& store = util::AnkiStore::getInstance();
    if (static_cast<size_t>(index) >= store.count()) {
      server->send(404, "application/json", "{\"error\":\"out of range\"}");
      return;
    }
    store.updateCardBack(static_cast<size_t>(index), doc["back"] | "");
    store.save();
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server->on("/api/anki/sync", HTTP_POST, [server] {
    if (SETTINGS.ankiConnectUrl[0] == '\0') {
      server->send(400, "application/json", "{\"error\":\"AnkiConnect URL not configured\"}");
      return;
    }

    auto& store = util::AnkiStore::getInstance();
    const auto cards = store.copyCards();
    if (cards.empty()) {
      server->send(200, "application/json", "{\"synced\":0,\"skipped\":0}");
      return;
    }

    // Build AnkiConnect addNotes payload.
    JsonDocument payload;
    payload["action"] = "addNotes";
    payload["version"] = 6;
    JsonObject params = payload["params"].to<JsonObject>();
    const char* deck = SETTINGS.ankiConnectDeck[0] != '\0' ? SETTINGS.ankiConnectDeck : "CrossPoint";
    JsonArray notes = params["notes"].to<JsonArray>();
    for (const auto& card : cards) {
      JsonObject note = notes.add<JsonObject>();
      note["deckName"] = deck;
      note["modelName"] = "Basic";
      JsonObject fields = note["fields"].to<JsonObject>();
      fields["Front"] = card.front.c_str();
      fields["Back"] = card.back.c_str();
      JsonArray tags = note["tags"].to<JsonArray>();
      tags.add("crosspoint");
      if (!card.context.empty()) {
        tags.add(card.context.c_str());
      }
      note["options"]["allowDuplicate"] = false;
    }

    std::string body;
    body.reserve(256 + cards.size() * 128);
    serializeJson(payload, body);

    const std::string url(SETTINGS.ankiConnectUrl);
    std::string response;
    if (!HttpDownloader::postJson(url, body, response)) {
      server->send(502, "application/json", "{\"error\":\"AnkiConnect unreachable\"}");
      return;
    }

    // Parse result array: each entry is a note ID (success) or null (duplicate/error).
    JsonDocument result;
    if (deserializeJson(result, response)) {
      server->send(502, "application/json", "{\"error\":\"Invalid AnkiConnect response\"}");
      return;
    }
    if (!result["error"].isNull()) {
      // as<const char*>() returns nullptr when "error" is present but not a
      // string (e.g. a number/bool); appending nullptr to std::string is UB.
      const char* errStr = result["error"].as<const char*>();
      std::string errMsg = "{\"error\":\"";
      errMsg += (errStr != nullptr) ? errStr : "unknown";
      errMsg += "\"}";
      server->send(502, "application/json", errMsg.c_str());
      return;
    }

    int synced = 0;
    int skipped = 0;
    JsonArray resultArr = result["result"];
    for (JsonVariant v : resultArr) {
      if (v.isNull()) {
        ++skipped;
      } else {
        ++synced;
      }
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"synced\":%d,\"skipped\":%d}", synced, skipped);
    LOG_INF("ANKI", "AnkiConnect sync: %d synced, %d skipped", synced, skipped);
    server->send(200, "application/json", resp);
  });
}

static bool shouldExposeAnkiHomeAction(core::HomeActionEntry::HomeActionContext ctx) {
  (void)ctx;
  return core::FeatureCatalog::isEnabled("anki_support");
}

static Activity* createAnkiHomeActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void* callbackCtx,
                                              void (*onBack)(void* ctx)) {
  (void)callbackCtx;
  (void)onBack;
  return new AnkiActivity(renderer, mappedInput);
}
#endif

void onStorageReady() { util::AnkiStore::getInstance().load(); }

}  // namespace

void registerFeature() {
#if ENABLE_ANKI_SUPPORT
  core::LifecycleEntry entry{};
  entry.onStorageReady = onStorageReady;
  core::LifecycleRegistry::add(entry);

  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "anki_plugin";
  webRouteEntry.shouldRegister = shouldRegisterAnkiPluginRoute;
  webRouteEntry.mountRoutes = mountAnkiRoutes;
  core::WebRouteRegistry::add(webRouteEntry);

  core::HomeActionEntry homeEntry{};
  homeEntry.actionId = "anki";
  homeEntry.shouldExpose = shouldExposeAnkiHomeAction;
  homeEntry.create = createAnkiHomeActionActivity;
  core::HomeActionRegistry::add(homeEntry);
#endif
}

}  // namespace features::anki
