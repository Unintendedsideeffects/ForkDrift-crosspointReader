#include "features/pokemon_party/Registration.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WebServer.h>
#include <mbedtls/base64.h>

#include <cstring>
#include <vector>

#include "core/features/FeatureCatalog.h"
#include "core/registries/WebRouteRegistry.h"
#include "util/PathUtils.h"
#include "util/PokemonBookDataStore.h"
#include "util/PokemonSpriteCache.h"
#include "util/PokemonTeamStore.h"

namespace features::pokemon_party {
namespace {

#if ENABLE_POKEMON_PARTY
bool shouldRegisterPokemonPartyApiRoute() { return core::FeatureCatalog::isEnabled("pokemon_party"); }

// Decode a base64 string into a heap buffer. Returns an empty vector on failure.
// Sprites are tiny (~1.2KB for a 96x96 1-bit BMP) so a single allocation is fine.
std::vector<uint8_t> decodeBase64(const char* b64, size_t b64Len) {
  if (b64 == nullptr || b64Len == 0) {
    return {};
  }
  size_t decodedLen = 0;
  // First call sizes the output buffer.
  int ret = mbedtls_base64_decode(nullptr, 0, &decodedLen, reinterpret_cast<const unsigned char*>(b64), b64Len);
  if ((ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) || decodedLen == 0) {
    return {};
  }
  std::vector<uint8_t> out(decodedLen);
  ret = mbedtls_base64_decode(out.data(), out.size(), &decodedLen, reinterpret_cast<const unsigned char*>(b64), b64Len);
  if (ret != 0) {
    return {};
  }
  out.resize(decodedLen);
  return out;
}

void mountPokemonRoutes(WebServer* server) {
  server->on("/api/book-pokemon", HTTP_GET, [server] {
    if (!server->hasArg("path")) {
      server->send(400, "text/plain", "Missing path");
      return;
    }
    String bookPath = PathUtils::urlDecode(server->arg("path"));
    if (!PathUtils::isValidSdPath(bookPath)) {
      server->send(400, "text/plain", "Invalid path");
      return;
    }
    bookPath = PathUtils::normalizePath(bookPath);
    if (PathUtils::pathContainsProtectedItem(bookPath)) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
    if (!Storage.exists(bookPath.c_str())) {
      server->send(404, "text/plain", "Book not found");
      return;
    }
    if (!PokemonBookDataStore::supportsBookPath(bookPath.c_str())) {
      server->send(400, "text/plain", "Unsupported book type");
      return;
    }
    JsonDocument response;
    response["path"] = bookPath;
    JsonDocument pokemonDoc;
    if (PokemonBookDataStore::loadPokemonDocument(bookPath.c_str(), pokemonDoc) &&
        pokemonDoc["pokemon"].is<JsonObject>()) {
      response["pokemon"] = pokemonDoc["pokemon"];
    } else {
      response["pokemon"] = nullptr;
    }
    String json;
    serializeJson(response, json);
    server->send(200, "application/json", json);
  });

  server->on("/api/book-pokemon", HTTP_PUT, [server] {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing body");
      return;
    }
    const String requestBody = server->arg("plain");
    JsonDocument request;
    if (deserializeJson(request, requestBody.c_str())) {
      server->send(400, "text/plain", "Invalid JSON body");
      return;
    }
    const String rawPath = request["path"] | "";
    if (rawPath.isEmpty()) {
      server->send(400, "text/plain", "Missing path");
      return;
    }
    if (!request["pokemon"].is<JsonObject>()) {
      server->send(400, "text/plain", "Missing pokemon object");
      return;
    }
    if (!PathUtils::isValidSdPath(rawPath)) {
      server->send(400, "text/plain", "Invalid path");
      return;
    }
    const String bookPath = PathUtils::normalizePath(rawPath);
    if (PathUtils::pathContainsProtectedItem(bookPath)) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
    if (!Storage.exists(bookPath.c_str())) {
      server->send(404, "text/plain", "Book not found");
      return;
    }
    if (!PokemonBookDataStore::supportsBookPath(bookPath.c_str())) {
      server->send(400, "text/plain", "Unsupported book type");
      return;
    }
    if (!PokemonBookDataStore::savePokemonDocument(bookPath.c_str(), request["pokemon"])) {
      server->send(500, "text/plain", "Failed to save pokemon data");
      return;
    }

    // Best-effort: while the device is online for this web session, pre-fetch and
    // convert the sprite for every evolution stage so the party theme and sleep
    // screen can render real Pokémon (instead of the Poké Ball placeholder) as the
    // reading level crosses each evolution threshold. Offline / SoftAP-only
    // sessions simply skip this; the renderers fall back gracefully.
    JsonVariantConst pokemon = request["pokemon"];
    PokemonSpriteCache::ensureSpriteById(pokemon["speciesId"] | 0, PokemonSpriteCache::kDefaultSpriteSize,
                                         PokemonSpriteCache::kDefaultSpriteSize);
    for (JsonVariantConst stage : pokemon["evolutionChain"].as<JsonArrayConst>()) {
      PokemonSpriteCache::ensureSpriteById(stage["speciesId"] | 0, PokemonSpriteCache::kDefaultSpriteSize,
                                           PokemonSpriteCache::kDefaultSpriteSize);
    }

    JsonDocument response;
    response["ok"] = true;
    response["path"] = bookPath;
    response["pokemon"] = request["pokemon"];
    String json;
    serializeJson(response, json);
    server->send(200, "application/json", json);
  });

  server->on("/api/book-pokemon", HTTP_DELETE, [server] {
    if (!server->hasArg("path")) {
      server->send(400, "text/plain", "Missing path");
      return;
    }
    String bookPath = PathUtils::urlDecode(server->arg("path"));
    if (!PathUtils::isValidSdPath(bookPath)) {
      server->send(400, "text/plain", "Invalid path");
      return;
    }
    bookPath = PathUtils::normalizePath(bookPath);
    if (PathUtils::pathContainsProtectedItem(bookPath)) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
    if (!Storage.exists(bookPath.c_str())) {
      server->send(404, "text/plain", "Book not found");
      return;
    }
    if (!PokemonBookDataStore::supportsBookPath(bookPath.c_str())) {
      server->send(400, "text/plain", "Unsupported book type");
      return;
    }
    if (!PokemonBookDataStore::deletePokemonDocument(bookPath.c_str())) {
      server->send(500, "text/plain", "Failed to delete pokemon data");
      return;
    }
    JsonDocument response;
    response["ok"] = true;
    response["path"] = bookPath;
    String json;
    serializeJson(response, json);
    server->send(200, "application/json", json);
  });

  // --- Prebaked team roster (offline-first): the browser builds a team of up to
  // six Pokémon, prebakes each member's data + sprites, and stores the roster
  // here so assignment later (browser or on-device) needs no network. ---
  server->on("/api/pokemon-team", HTTP_GET, [server] {
    JsonDocument doc;
    String json;
    if (PokemonTeamStore::loadTeamDocument(doc) && doc["team"].is<JsonArray>()) {
      serializeJson(doc["team"], json);
    } else {
      json = "[]";
    }
    server->send(200, "application/json", json);
  });

  server->on("/api/pokemon-team", HTTP_PUT, [server] {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing body");
      return;
    }
    JsonDocument request;
    if (deserializeJson(request, server->arg("plain").c_str())) {
      server->send(400, "text/plain", "Invalid JSON body");
      return;
    }
    if (!request["team"].is<JsonArray>()) {
      server->send(400, "text/plain", "Missing team array");
      return;
    }
    if (!PokemonTeamStore::saveTeamDocument(request["team"])) {
      server->send(500, "text/plain", "Failed to save team");
      return;
    }
    server->send(200, "application/json", "{\"ok\":true}");
  });

  // Receive one browser-converted 1-bit sprite BMP and cache it by species id, so
  // the device can render real Pokémon (and their evolutions) entirely offline.
  server->on("/api/pokemon-sprite", HTTP_POST, [server] {
    if (!server->hasArg("plain")) {
      server->send(400, "text/plain", "Missing body");
      return;
    }
    JsonDocument request;
    if (deserializeJson(request, server->arg("plain").c_str())) {
      server->send(400, "text/plain", "Invalid JSON body");
      return;
    }
    const int speciesId = request["speciesId"] | 0;
    const char* bmpBase64 = request["bmpBase64"] | "";
    if (speciesId <= 0 || bmpBase64[0] == '\0') {
      server->send(400, "text/plain", "Missing speciesId or bmpBase64");
      return;
    }
    const std::vector<uint8_t> bmp = decodeBase64(bmpBase64, strlen(bmpBase64));
    if (bmp.empty()) {
      server->send(400, "text/plain", "Invalid base64");
      return;
    }
    if (!PokemonSpriteCache::saveSpriteBmp(speciesId, bmp.data(), bmp.size())) {
      server->send(500, "text/plain", "Failed to save sprite");
      return;
    }
    server->send(200, "application/json", "{\"ok\":true}");
  });
}
#endif

}  // namespace

void registerFeature() {
#if ENABLE_POKEMON_PARTY
  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "pokemon_party_api";
  webRouteEntry.shouldRegister = shouldRegisterPokemonPartyApiRoute;
  webRouteEntry.mountRoutes = mountPokemonRoutes;
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

}  // namespace features::pokemon_party
