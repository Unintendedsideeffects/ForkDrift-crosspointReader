#include <HalStorage.h>

#include <string>

#include "util/PokemonSpriteCache.h"

namespace PokemonSpriteCache {

std::string spritePath(const int speciesId) {
  if (speciesId <= 0) {
    return "";
  }
  return "/.crosspoint/pokemon/sprite_" + std::to_string(speciesId) + ".bmp";
}

bool isCached(const int speciesId) {
  const std::string path = spritePath(speciesId);
  return !path.empty() && Storage.exists(path.c_str());
}

bool ensureSpriteById(const int, const int, const int) { return false; }

bool saveSpriteBmp(const int, const uint8_t*, const size_t) { return true; }

}  // namespace PokemonSpriteCache
