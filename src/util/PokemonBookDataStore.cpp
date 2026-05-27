#include "util/PokemonBookDataStore.h"

#include <ArduinoJson.h>
#include <BookCachePath.h>
#include <FsFileJsonReader.h>
#include <HalStorage.h>

#include <memory>

namespace {
constexpr char kPokemonDataFileName[] = "/pokemon.json";
}  // namespace

bool PokemonBookDataStore::supportsBookPath(const std::string& bookPath) {
  return BookCachePath::prefixForBookPath(bookPath) != nullptr;
}

bool PokemonBookDataStore::resolveCachePath(const std::string& bookPath, std::string& outCachePath) {
  return BookCachePath::resolve("/.crosspoint", bookPath, outCachePath);
}

std::string PokemonBookDataStore::getPokemonDataPath(const std::string& bookPath) {
  std::string cachePath;
  if (!resolveCachePath(bookPath, cachePath)) {
    return "";
  }
  return cachePath + kPokemonDataFileName;
}

bool PokemonBookDataStore::loadPokemonDocument(const std::string& bookPath, JsonDocument& doc) {
  const std::string pokemonDataPath = getPokemonDataPath(bookPath);
  if (pokemonDataPath.empty() || !Storage.exists(pokemonDataPath.c_str())) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("PKM", pokemonDataPath, file)) {
    return false;
  }

  FsFileJsonReader reader(file);
  const DeserializationError error = deserializeJson(doc, reader);
  file.close();
  return !error;
}

bool PokemonBookDataStore::savePokemonDocument(const std::string& bookPath, JsonVariantConst pokemonData) {
  std::string cachePath;
  if (!resolveCachePath(bookPath, cachePath)) {
    return false;
  }

  Storage.mkdir("/.crosspoint");
  Storage.mkdir(cachePath.c_str());

  JsonDocument doc;
  doc["pokemon"].set(pokemonData);

  const size_t jsonSize = measureJson(doc);
  std::unique_ptr<char[]> jsonBuffer(new char[jsonSize + 1]);
  serializeJson(doc, jsonBuffer.get(), jsonSize + 1);
  return Storage.writeFile((cachePath + kPokemonDataFileName).c_str(), jsonBuffer.get());
}

bool PokemonBookDataStore::deletePokemonDocument(const std::string& bookPath) {
  const std::string pokemonDataPath = getPokemonDataPath(bookPath);
  if (pokemonDataPath.empty()) {
    return false;
  }

  if (!Storage.exists(pokemonDataPath.c_str())) {
    return true;
  }

  return Storage.remove(pokemonDataPath.c_str());
}
