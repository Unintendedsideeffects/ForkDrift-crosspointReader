#include <ArduinoJson.h>

#include "util/PokemonSpriteCache.h"
#include "util/PokemonTeamStore.h"

namespace PokemonSpriteCache {

bool ensureSpriteById(const int, const int, const int) { return false; }

bool saveSpriteBmp(const int, const uint8_t*, const size_t) { return true; }

}  // namespace PokemonSpriteCache

namespace PokemonTeamStore {

bool loadTeamDocument(JsonDocument& doc) {
  doc.clear();
  return false;
}

bool saveTeamDocument(const JsonVariantConst) { return true; }

}  // namespace PokemonTeamStore
