#include <HalStorage.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "core/features/FeatureModules.h"
#include "util/BookProgressDataStore.h"
#include "util/PokemonSpriteCache.h"

namespace core {
FeatureModules::RecentBookDataResult FeatureModules::resolveRecentBookData(const std::string&) { return {}; }
}  // namespace core

bool BookProgressDataStore::supportsBookPath(const std::string&) { return true; }
bool BookProgressDataStore::resolveCachePath(const std::string&, std::string& outCachePath) {
  outCachePath.clear();
  return false;
}
bool BookProgressDataStore::loadProgress(const std::string& bookPath, ProgressData& outProgress) {
  outProgress = {};
  const char* lastDash = ::strrchr(bookPath.c_str(), '-');
  if (lastDash != nullptr) {
    outProgress.percent = static_cast<float>((std::atoi(lastDash + 1) + 1) * 14);
  } else {
    outProgress.percent = 57.0f;
  }
  return true;
}
const char* BookProgressDataStore::kindName(BookKind) { return "epub"; }
std::string BookProgressDataStore::formatPositionLabel(const ProgressData&) { return ""; }

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

bool ensureSprite(int, const std::string&, int, int) { return false; }
bool ensureSpriteById(int, int, int) { return false; }
bool saveSpriteBmp(int, const uint8_t*, size_t) { return false; }
}  // namespace PokemonSpriteCache
