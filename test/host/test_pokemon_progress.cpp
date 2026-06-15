#include <ArduinoJson.h>

#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"
#include "util/PokemonBookDataStore.h"
#include "util/PokemonPartySprites.h"
#include "util/PokemonProgress.h"
#include "util/PokemonTeamStore.h"
#include "util/RecentBooksStore.h"

TEST_CASE("PokemonProgress maps team slot by recent-books index order") {
  Storage.reset();

  CHECK(Storage.writeFile(
      "/.crosspoint/pokemon/team.json",
      R"({"team":[{"id":842,"name":"appletun","speciesId":842},{"id":25,"name":"pikachu","speciesId":25}]})"));
  CHECK(Storage.writeFile("/Books/first.epub", "epub"));
  CHECK(Storage.writeFile("/Books/second.epub", "epub"));
  CHECK(Storage.writeFile("/.crosspoint/recent.json",
                          R"({"books":[{"path":"/Books/first.epub","title":"First","author":"","coverBmpPath":""},)"
                          R"({"path":"/Books/second.epub","title":"Second","author":"","coverBmpPath":""}]})"));
  CHECK(RECENT_BOOKS.loadFromFile());

  const PokemonAssignment first = PokemonProgress::loadForBook("/Books/first.epub");
  const PokemonAssignment second = PokemonProgress::loadForBook("/Books/second.epub");
  CHECK(first.speciesId == 842);
  CHECK(second.speciesId == 25);
}

TEST_CASE("PokemonProgress falls back to team roster by recent-books index") {
  Storage.reset();

  CHECK(Storage.writeFile("/.crosspoint/pokemon/team.json",
                          R"({"team":[{"id":842,"name":"appletun","speciesId":842,"speciesName":"appletun"}]})"));
  CHECK(Storage.writeFile("/Books/demo.epub", "epub"));
  RECENT_BOOKS.addBook("/Books/demo.epub", "Demo", "Author", "");

  const PokemonAssignment assignment = PokemonProgress::loadForBook("/Books/demo.epub");
  CHECK(assignment.valid);
  CHECK(assignment.speciesId == 842);
  CHECK(assignment.name == "appletun");
}

TEST_CASE("PokemonProgress prefers per-book pokemon.json over team fallback") {
  Storage.reset();

  CHECK(Storage.writeFile("/.crosspoint/pokemon/team.json",
                          R"({"team":[{"id":842,"name":"appletun","speciesId":842}]})"));
  CHECK(Storage.writeFile("/Books/demo.epub", "epub"));
  RECENT_BOOKS.addBook("/Books/demo.epub", "Demo", "Author", "");

  JsonDocument pokemonDoc;
  JsonObject pokemon = pokemonDoc["pokemon"].to<JsonObject>();
  pokemon["id"] = 25;
  pokemon["speciesId"] = 25;
  pokemon["name"] = "pikachu";
  CHECK(PokemonBookDataStore::savePokemonDocument("/Books/demo.epub", pokemon));

  const PokemonAssignment assignment = PokemonProgress::loadForBook("/Books/demo.epub");
  CHECK(assignment.valid);
  CHECK(assignment.speciesId == 25);
  CHECK(assignment.name == "pikachu");
}

TEST_CASE("PokemonProgress does not treat unknown evolution requirements as level 1") {
  Storage.reset();

  JsonDocument pokemonDoc;
  JsonObject pokemon = pokemonDoc["pokemon"].to<JsonObject>();
  pokemon["id"] = 172;
  pokemon["speciesId"] = 172;
  pokemon["name"] = "pichu";
  JsonArray chain = pokemon["evolutionChain"].to<JsonArray>();
  JsonObject base = chain.add<JsonObject>();
  base["speciesId"] = 172;
  base["name"] = "pichu";
  base["minLevel"] = nullptr;
  JsonObject middle = chain.add<JsonObject>();
  middle["speciesId"] = 25;
  middle["name"] = "pikachu";
  middle["minLevel"] = nullptr;
  JsonObject final = chain.add<JsonObject>();
  final["speciesId"] = 26;
  final["name"] = "raichu";
  final["minLevel"] = nullptr;

  CHECK(PokemonBookDataStore::savePokemonDocument("/Books/demo.epub", pokemon));

  const PokemonAssignment assignment = PokemonProgress::loadForBook("/Books/demo.epub");
  REQUIRE(assignment.valid);
  REQUIRE(assignment.chain.size() == 3);
  CHECK(assignment.chain[0].minLevel == 1);
  CHECK(assignment.chain[1].minLevel == 33);
  CHECK(assignment.chain[2].minLevel == 66);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 1) == 172);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 32) == 172);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 33) == 25);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 65) == 25);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 66) == 26);
}

TEST_CASE("PokemonProgress keeps a two-stage unknown evolution at level 50") {
  Storage.reset();

  JsonDocument pokemonDoc;
  JsonObject pokemon = pokemonDoc["pokemon"].to<JsonObject>();
  pokemon["id"] = 133;
  pokemon["speciesId"] = 133;
  pokemon["name"] = "eevee";
  JsonArray chain = pokemon["evolutionChain"].to<JsonArray>();
  JsonObject base = chain.add<JsonObject>();
  base["speciesId"] = 133;
  base["name"] = "eevee";
  base["minLevel"] = nullptr;
  JsonObject evolved = chain.add<JsonObject>();
  evolved["speciesId"] = 134;
  evolved["name"] = "vaporeon";
  evolved["minLevel"] = nullptr;

  CHECK(PokemonBookDataStore::savePokemonDocument("/Books/demo.epub", pokemon));

  const PokemonAssignment assignment = PokemonProgress::loadForBook("/Books/demo.epub");
  REQUIRE(assignment.valid);
  REQUIRE(assignment.chain.size() == 2);
  CHECK(assignment.chain[0].minLevel == 1);
  CHECK(assignment.chain[1].minLevel == 50);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 49) == 133);
  CHECK(PokemonProgress::activeSpeciesId(assignment, 50) == 134);
}

TEST_CASE("PokemonPartySprites reports missing sprites and stable fingerprint") {
  Storage.reset();

  CHECK(Storage.writeFile("/.crosspoint/pokemon/team.json", R"({"team":[{"id":25,"name":"pikachu","speciesId":25}]})"));
  CHECK(Storage.writeFile("/Books/demo.epub", "epub"));
  RECENT_BOOKS.addBook("/Books/demo.epub", "Demo", "Author", "");

  const std::vector<RecentBook> books = RECENT_BOOKS.getBooks();
  const PokemonPartySprites::SyncResult first = PokemonPartySprites::syncPartySprites(books);
  CHECK(first.missingCount == 1);
  CHECK(first.newlyCachedCount == 0);
  CHECK(first.cacheFingerprint != 0);

  const PokemonPartySprites::SyncResult second = PokemonPartySprites::syncPartySprites(books);
  CHECK(second.missingCount == 1);
  CHECK(second.cacheFingerprint == first.cacheFingerprint);
}

TEST_CASE("PokemonPartySprites refresh stops after max retries without cache change") {
  PokemonPartySprites::RefreshState state;
  PokemonPartySprites::SyncResult sync;
  sync.missingCount = 1;
  sync.cacheFingerprint = 0xABCDEF01u;

  {
    auto dec = PokemonPartySprites::decideRefresh(state, sync);
    CHECK_FALSE(dec.forceFullRefresh);
    CHECK(dec.requestRedraw);
  }
  {
    auto dec = PokemonPartySprites::decideRefresh(state, sync);
    CHECK_FALSE(dec.forceFullRefresh);
    CHECK(dec.requestRedraw);
  }
  {
    auto dec = PokemonPartySprites::decideRefresh(state, sync);
    CHECK_FALSE(dec.forceFullRefresh);
    CHECK(dec.requestRedraw);
  }
  {
    auto dec = PokemonPartySprites::decideRefresh(state, sync);
    CHECK_FALSE(dec.forceFullRefresh);
    CHECK_FALSE(dec.requestRedraw);
  }
  CHECK(state.retries == PokemonPartySprites::kMaxRefreshRetries);
}

TEST_CASE("PokemonPartySprites refresh resets when cache fingerprint changes") {
  PokemonPartySprites::RefreshState state;
  state.retries = PokemonPartySprites::kMaxRefreshRetries;
  state.cacheFingerprint = 100u;

  PokemonPartySprites::SyncResult sync;
  sync.cacheFingerprint = 200u;
  sync.missingCount = 0;

  const PokemonPartySprites::RefreshDecision decision = PokemonPartySprites::decideRefresh(state, sync);
  CHECK(decision.forceFullRefresh);
  CHECK(decision.requestRedraw);
  CHECK(state.retries == 0);
  CHECK(state.cacheFingerprint == 200u);
}

TEST_CASE("PokemonPartySprites refresh resets when a sprite is newly cached") {
  PokemonPartySprites::RefreshState state;
  state.retries = 2;

  PokemonPartySprites::SyncResult sync;
  sync.newlyCachedCount = 1;
  sync.cacheFingerprint = 555u;

  const PokemonPartySprites::RefreshDecision decision = PokemonPartySprites::decideRefresh(state, sync);
  CHECK(decision.forceFullRefresh);
  CHECK(decision.requestRedraw);
  CHECK(state.retries == 0);
  CHECK(state.cacheFingerprint == 555u);
}

TEST_CASE("PokemonPartySprites loads cached sprite BMP for team assignment") {
  Storage.reset();

  const char* bmpHeader =
      "BM\xBE\x04\x00\x00\x00\x00\x00\x00\x3E\x00\x00\x00\x28\x00\x00\x00"
      "\x60\x00\x00\x00\xA0\xFF\xFF\xFF\x01\x00\x01\x00\x00\x00\x00\x00"
      "\x80\x04\x00\x00\x13\x0B\x00\x00\x13\x0B\x00\x00\x02\x00\x00\x00"
      "\x02\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xFF\x00";
  std::string bmp(bmpHeader, bmpHeader + 62);
  bmp.resize(1214, '\xFF');
  CHECK(Storage.writeFile("/.crosspoint/pokemon/sprite_25.bmp", bmp));
  CHECK(Storage.writeFile("/.crosspoint/pokemon/team.json", R"({"team":[{"id":25,"name":"pikachu","speciesId":25}]})"));
  CHECK(Storage.writeFile("/Books/demo.epub", "epub"));
  RECENT_BOOKS.addBook("/Books/demo.epub", "Demo", "Author", "");

  const std::vector<RecentBook> books = RECENT_BOOKS.getBooks();
  const PokemonPartySprites::SyncResult sync = PokemonPartySprites::syncPartySprites(books);
  CHECK(sync.missingCount == 0);
  CHECK(sync.newlyCachedCount == 0);
  CHECK(sync.cacheFingerprint != 0);
}
