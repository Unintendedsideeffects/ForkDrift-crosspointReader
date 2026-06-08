#include "util/PokemonSpriteCache.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <WiFi.h>

#include <string>

#include "network/HttpDownloader.h"

namespace {
constexpr char kPokemonDir[] = "/.crosspoint/pokemon";
}  // namespace

namespace PokemonSpriteCache {

std::string spritePath(int speciesId) {
  if (speciesId <= 0) {
    return "";
  }
  return std::string(kPokemonDir) + "/sprite_" + std::to_string(speciesId) + ".bmp";
}

bool isCached(int speciesId) {
  const std::string path = spritePath(speciesId);
  return !path.empty() && Storage.exists(path.c_str());
}

bool ensureSprite(int speciesId, const std::string& spriteUrl, int targetWidth, int targetHeight) {
  if (speciesId <= 0) {
    return false;
  }
  if (isCached(speciesId)) {
    return true;
  }
  if (spriteUrl.empty() || WiFi.status() != WL_CONNECTED) {
    return false;  // can't fetch offline; caller draws a placeholder instead
  }

  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kPokemonDir);

  const std::string tmpPng = std::string(kPokemonDir) + "/_dl_" + std::to_string(speciesId) + ".png";
  if (HttpDownloader::downloadToFile(spriteUrl, tmpPng) != HttpDownloader::OK) {
    LOG_ERR("PKM", "sprite download failed id=%d", speciesId);
    Storage.remove(tmpPng.c_str());
    return false;
  }

  HalFile pngFile;
  if (!Storage.openFileForRead("PKM", tmpPng, pngFile)) {
    Storage.remove(tmpPng.c_str());
    return false;
  }
  const std::string outPath = spritePath(speciesId);
  HalFile bmpFile;
  if (!Storage.openFileForWrite("PKM", outPath, bmpFile)) {
    pngFile.close();
    Storage.remove(tmpPng.c_str());
    return false;
  }

  // Pixel sprites are small transparent PNGs; thresholding to 1-bit yields a
  // crisp Game Boy-style silhouette that suits the e-ink panel.
  const bool ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, bmpFile, targetWidth, targetHeight, false);
  pngFile.close();
  bmpFile.close();
  Storage.remove(tmpPng.c_str());

  if (!ok) {
    LOG_ERR("PKM", "sprite convert failed id=%d", speciesId);
    Storage.remove(outPath.c_str());
    return false;
  }
  LOG_INF("PKM", "cached sprite id=%d", speciesId);
  return true;
}

bool ensureSpriteById(int speciesId, int targetWidth, int targetHeight) {
  if (speciesId <= 0) {
    return false;
  }
  if (isCached(speciesId)) {
    return true;
  }
  // Canonical PokéAPI pixel sprite (front default) for the species.
  const std::string url =
      "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/" + std::to_string(speciesId) + ".png";
  return ensureSprite(speciesId, url, targetWidth, targetHeight);
}

}  // namespace PokemonSpriteCache
