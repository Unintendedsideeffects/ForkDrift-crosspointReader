#include <cmath>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "include/BookCachePath.h"
#include "lib/Serialization/Serialization.h"
#include "src/network/RecentBookJson.h"
#include "src/util/PokemonBookDataStore.h"
#include "test/mock/HalStorage.h"

namespace {
std::string buildCachePath(const char* prefix, const std::string& bookPath) {
  return BookCachePath::build("/.crosspoint", prefix, bookPath);
}

void writeTxtIndexFile(const std::string& path, uint32_t totalPages, uint8_t markdownFlag = 0) {
  FsFile f;
  CHECK(Storage.openFileForWrite("TST", path, f));
  serialization::writePod(f, static_cast<uint32_t>(0x54585449));
  serialization::writePod(f, static_cast<uint8_t>(3));
  serialization::writePod(f, static_cast<uint32_t>(0));
  serialization::writePod(f, static_cast<int32_t>(480));
  serialization::writePod(f, static_cast<int32_t>(20));
  serialization::writePod(f, static_cast<int32_t>(0));
  serialization::writePod(f, static_cast<int32_t>(0));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, markdownFlag);
  serialization::writePod(f, totalPages);
  f.close();
}
}  // namespace

TEST_CASE("testRecentBookJsonIncludesPokemon") {
  Storage.reset();

  const std::string bookPath = "/books/recent.txt";
  const std::string cachePath = buildCachePath("txt_", bookPath);
  CHECK(Storage.writeFile(bookPath.c_str(), "demo"));

  FsFile progressFile;
  CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
  const uint8_t progressBytes[4] = {9, 0, 0, 0};
  progressFile.write(progressBytes, sizeof(progressBytes));
  progressFile.close();
  writeTxtIndexFile(cachePath + "/index.bin", 40);

  JsonDocument pokemonDoc;
  JsonObject pokemon = pokemonDoc["pokemon"].to<JsonObject>();
  pokemon["id"] = 133;
  pokemon["name"] = "eevee";
  JsonArray types = pokemon["types"].to<JsonArray>();
  types.add("normal");
  CHECK(PokemonBookDataStore::savePokemonDocument(bookPath.c_str(), pokemon));

  const RecentBook book{bookPath, "Recent Demo", "Unit Tester", "/covers/recent.bmp"};
  const String enrichedJson = network::buildRecentBookJson(book, true);

  JsonDocument enrichedDoc;
  CHECK(!deserializeJson(enrichedDoc, enrichedJson.c_str()));
  CHECK(std::string(enrichedDoc["path"] | "") == bookPath);
  CHECK(std::string(enrichedDoc["title"] | "") == "Recent Demo");
  CHECK(std::string(enrichedDoc["author"] | "") == "Unit Tester");
  CHECK(enrichedDoc["hasCover"] == true);
  CHECK(std::string(enrichedDoc["last_position"] | "") == "10/40 25%");
  CHECK(std::string(enrichedDoc["progress"]["format"] | "") == "txt");
  CHECK(enrichedDoc["progress"]["page"] == 10);
  CHECK(enrichedDoc["progress"]["pageCount"] == 40);
  CHECK(std::fabs(static_cast<float>(enrichedDoc["progress"]["percent"] | 0.0f) - 25.0f) < 0.01f);
  CHECK(enrichedDoc["pokemon"]["id"] == 133);
  CHECK(std::string(enrichedDoc["pokemon"]["name"] | "") == "eevee");

  const String plainJson = network::buildRecentBookJson(book, false);
  JsonDocument plainDoc;
  CHECK(!deserializeJson(plainDoc, plainJson.c_str()));
  CHECK(plainDoc["pokemon"].isNull());
}
