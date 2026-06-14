#include <WebServer.h>

#include <string>

#include "doctest/doctest.h"
#include "src/core/registries/WebRouteRegistry.h"
#include "src/features/pokemon_party/Registration.h"
#include "src/util/PokemonBookDataStore.h"
#include "test/mock/Arduino.h"
#include "test/mock/HalStorage.h"

TEST_CASE("testPokemonBookDataStore") {
  Storage.reset();

  std::string cachePath;
  CHECK(PokemonBookDataStore::resolveCachePath("/books/demo.epub", cachePath));
  CHECK(cachePath.find("/.crosspoint/epub_") == 0);
  CHECK(PokemonBookDataStore::supportsBookPath("/books/demo.xtc"));
  CHECK(PokemonBookDataStore::supportsBookPath("/books/demo.xtch"));
  CHECK(PokemonBookDataStore::supportsBookPath("/books/demo.txt"));
  CHECK(PokemonBookDataStore::supportsBookPath("/books/demo.md"));
  CHECK(!PokemonBookDataStore::supportsBookPath("/books/demo.pdf"));

  JsonDocument writeDoc;
  JsonObject pokemon = writeDoc["pokemon"].to<JsonObject>();
  pokemon["id"] = 1;
  pokemon["name"] = "bulbasaur";
  pokemon["starterChoice"] = true;

  CHECK(PokemonBookDataStore::savePokemonDocument("/books/demo.epub", pokemon));

  JsonDocument readDoc;
  CHECK(PokemonBookDataStore::loadPokemonDocument("/books/demo.epub", readDoc));
  CHECK(readDoc["pokemon"]["id"] == 1);
  CHECK(std::string(readDoc["pokemon"]["name"] | "") == "bulbasaur");
  CHECK(readDoc["pokemon"]["starterChoice"] == true);

  CHECK(PokemonBookDataStore::deletePokemonDocument("/books/demo.epub"));
  JsonDocument deletedDoc;
  CHECK(!PokemonBookDataStore::loadPokemonDocument("/books/demo.epub", deletedDoc));

  JsonDocument missingDoc;
  CHECK(!PokemonBookDataStore::loadPokemonDocument("/books/missing.epub", missingDoc));
}

TEST_CASE("testPokemonPartyApiRoutes") {
  Storage.reset();

  features::pokemon_party::registerFeature();
  CHECK(core::WebRouteRegistry::shouldRegister("pokemon_party_api"));

  WebServer server;
  core::WebRouteRegistry::mountAll(&server);
  CHECK(server.hasRoute("/api/book-pokemon", HTTP_GET));
  CHECK(server.hasRoute("/api/book-pokemon", HTTP_PUT));
  CHECK(server.hasRoute("/api/book-pokemon", HTTP_DELETE));

  server.setRequest(HTTP_GET, "/api/book-pokemon");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Missing path");

  server.setRequest(HTTP_GET, "/api/book-pokemon");
  server.setArg("path", "../relative.epub");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Invalid path");

  server.setRequest(HTTP_GET, "/api/book-pokemon");
  server.setArg("path", "/.crosspoint/private.epub");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 403);
  CHECK(server.response().body == "Cannot access protected items");

  server.setRequest(HTTP_GET, "/api/book-pokemon");
  server.setArg("path", "/books/missing.epub");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 404);
  CHECK(server.response().body == "Book not found");

  CHECK(Storage.writeFile("/books/demo.pdf", "demo"));
  server.setRequest(HTTP_GET, "/api/book-pokemon");
  server.setArg("path", "/books/demo.pdf");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Unsupported book type");

  server.setRequest(HTTP_PUT, "/api/book-pokemon");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Missing body");

  server.setRequest(HTTP_PUT, "/api/book-pokemon");
  server.setBody("{");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Invalid JSON body");

  CHECK(Storage.writeFile("/books/demo.epub", "demo"));
  server.setRequest(HTTP_PUT, "/api/book-pokemon");
  server.setBody(
      "{\"path\":\"/books/demo.epub\",\"pokemon\":{\"id\":25,\"name\":\"pikachu\",\"types\":[\"electric\"],"
      "\"sleepImagePath\":\"/sleep/pokedex/party/party_demo.bmp\","
      "\"partyVisualPath\":\"/sleep/pokedex/party/party_visual_demo.bmp\"}}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);

  JsonDocument saveResponse;
  CHECK(!deserializeJson(saveResponse, server.response().body.c_str()));
  CHECK(std::string(saveResponse["path"] | "") == "/books/demo.epub");
  CHECK(saveResponse["pokemon"]["id"] == 25);
  CHECK(std::string(saveResponse["pokemon"]["name"] | "") == "pikachu");
  CHECK(std::string(saveResponse["pokemon"]["sleepImagePath"] | "") == "/sleep/pokedex/party/party_demo.bmp");
  CHECK(std::string(saveResponse["pokemon"]["partyVisualPath"] | "") == "/sleep/pokedex/party/party_visual_demo.bmp");

  JsonDocument storedDoc;
  CHECK(PokemonBookDataStore::loadPokemonDocument("/books/demo.epub", storedDoc));
  CHECK(storedDoc["pokemon"]["id"] == 25);
  CHECK(std::string(storedDoc["pokemon"]["partyVisualPath"] | "") == "/sleep/pokedex/party/party_visual_demo.bmp");

  server.setRequest(HTTP_GET, "/api/book-pokemon");
  server.setArg("path", "/books/demo.epub");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);

  JsonDocument getResponse;
  CHECK(!deserializeJson(getResponse, server.response().body.c_str()));
  CHECK(std::string(getResponse["path"] | "") == "/books/demo.epub");
  CHECK(getResponse["pokemon"]["id"] == 25);
  CHECK(std::string(getResponse["pokemon"]["partyVisualPath"] | "") == "/sleep/pokedex/party/party_visual_demo.bmp");

  server.setRequest(HTTP_DELETE, "/api/book-pokemon");
  server.setArg("path", "/books/demo.epub");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);

  JsonDocument deleteResponse;
  CHECK(!deserializeJson(deleteResponse, server.response().body.c_str()));
  CHECK(deleteResponse["ok"] == true);
  CHECK(std::string(deleteResponse["path"] | "") == "/books/demo.epub");

  JsonDocument clearedDoc;
  CHECK(!PokemonBookDataStore::loadPokemonDocument("/books/demo.epub", clearedDoc));

  server.setRequest(HTTP_GET, "/api/book-pokemon");
  server.setArg("path", "/books/demo.epub");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);

  JsonDocument clearedResponse;
  CHECK(!deserializeJson(clearedResponse, server.response().body.c_str()));
  CHECK(clearedResponse["pokemon"].isNull());
}

TEST_CASE("testPokemonTeamBaseFormValidation") {
  Storage.reset();
  features::pokemon_party::registerFeature();
  WebServer server;
  core::WebRouteRegistry::mountAll(&server);

  // Evolved form (charizard, speciesId=6, chain starts at charmander=4) must be rejected.
  server.setRequest(HTTP_PUT, "/api/pokemon-team");
  server.setBody(
      "{\"team\":[{\"id\":6,\"speciesId\":6,\"name\":\"charizard\","
      "\"evolutionChain\":[{\"order\":1,\"speciesId\":4,\"name\":\"charmander\",\"minLevel\":null},"
      "{\"order\":2,\"speciesId\":5,\"name\":\"charmeleon\",\"minLevel\":16},"
      "{\"order\":3,\"speciesId\":6,\"name\":\"charizard\",\"minLevel\":36}]}]}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Only base-form Pokemon may be added to the team");

  // Base form (charmander, speciesId=4, chain[0].speciesId=4) must be accepted.
  server.setRequest(HTTP_PUT, "/api/pokemon-team");
  server.setBody(
      "{\"team\":[{\"id\":4,\"speciesId\":4,\"name\":\"charmander\","
      "\"evolutionChain\":[{\"order\":1,\"speciesId\":4,\"name\":\"charmander\",\"minLevel\":null},"
      "{\"order\":2,\"speciesId\":5,\"name\":\"charmeleon\",\"minLevel\":16},"
      "{\"order\":3,\"speciesId\":6,\"name\":\"charizard\",\"minLevel\":36}]}]}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);

  // No-evolution Pokemon (empty chain) must be accepted (can't verify, allow through).
  server.setRequest(HTTP_PUT, "/api/pokemon-team");
  server.setBody("{\"team\":[{\"id\":132,\"speciesId\":132,\"name\":\"ditto\",\"evolutionChain\":[]}]}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);

  // Mixed team: one valid base form + one evolved form must be rejected.
  server.setRequest(HTTP_PUT, "/api/pokemon-team");
  server.setBody(
      "{\"team\":["
      "{\"id\":4,\"speciesId\":4,\"name\":\"charmander\","
      "\"evolutionChain\":[{\"order\":1,\"speciesId\":4,\"name\":\"charmander\",\"minLevel\":null}]},"
      "{\"id\":6,\"speciesId\":6,\"name\":\"charizard\","
      "\"evolutionChain\":[{\"order\":1,\"speciesId\":4,\"name\":\"charmander\",\"minLevel\":null},"
      "{\"order\":3,\"speciesId\":6,\"name\":\"charizard\",\"minLevel\":36}]}"
      "]}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
}
