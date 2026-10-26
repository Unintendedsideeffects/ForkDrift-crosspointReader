#include <cstring>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "network/server/AssetReadApi.h"
#include "test/mock/HalStorage.h"

TEST_CASE("asset read api validates and resolves cover paths") {
  Storage.reset();

  const std::vector<RecentBook> books = {
      {"/books/known.epub", "Known", "Author", "/covers/known.bmp"},
  };

  CHECK(network::resolveCoverAssetPath("", books, nullptr).statusCode == 400);
  CHECK(network::resolveCoverAssetPath("../bad", books, nullptr).statusCode == 400);
  CHECK(network::resolveCoverAssetPath("/.crosspoint/secret.epub", books, nullptr).statusCode == 403);
  CHECK(network::resolveCoverAssetPath("/books/missing.epub", books, nullptr).statusCode == 404);
}

TEST_CASE("asset read api returns recent-book cover path when cover file exists") {
  Storage.reset();
  CHECK(Storage.writeFile("/covers/known.bmp", "bmp-data"));

  const std::vector<RecentBook> books = {
      {"/books/known.epub", "Known", "Author", "/covers/known.bmp"},
  };

  const auto result = network::resolveCoverAssetPath("/books/known.epub", books, nullptr);
  CHECK(result.statusCode == 200);
  CHECK(std::strcmp(result.contentType, "image/bmp") == 0);
  CHECK(result.resolvedPath == "/covers/known.bmp");
}

TEST_CASE("asset read api falls back to resolver and surfaces missing cover files") {
  Storage.reset();

  const std::vector<RecentBook> books;
  auto fallback = [](const String& bookPath, std::string& coverPath) {
    if (bookPath != "/books/fallback.epub") {
      return false;
    }
    coverPath = "/covers/fallback.bmp";
    return true;
  };

  auto missingCover = network::resolveCoverAssetPath("/books/fallback.epub", books, fallback);
  CHECK(missingCover.statusCode == 404);
  CHECK(missingCover.body == "Cover file not found");

  CHECK(Storage.writeFile("/covers/fallback.bmp", "bmp-data"));
  const auto ok = network::resolveCoverAssetPath("/books/fallback.epub", books, fallback);
  CHECK(ok.statusCode == 200);
  CHECK(ok.resolvedPath == "/covers/fallback.bmp");
}

// Regression: recent books store "thumb_[HEIGHT].bmp" templates. /api/cover used
// to Storage.exists() the literal template, so every cover 404'd and the Android
// app and web UI showed none.
TEST_CASE("asset read api expands the [HEIGHT] thumb template, best size first") {
  Storage.reset();
  const std::vector<RecentBook> books = {
      {"/books/t.epub", "T", "A", "/.crosspoint/epub_1/thumb_[HEIGHT].bmp"},
  };
  const std::vector<std::string> suffixes = {"W300_H450", "226", "W120_H180"};

  CHECK(network::resolveCoverAssetPath("/books/t.epub", books, nullptr, suffixes).statusCode == 404);
  CHECK(network::resolveCoverAssetPath("/books/t.epub", books, nullptr).statusCode == 404);

  CHECK(Storage.writeFile("/.crosspoint/epub_1/thumb_W120_H180.bmp", "small"));
  CHECK(network::resolveCoverAssetPath("/books/t.epub", books, nullptr, suffixes).resolvedPath ==
        "/.crosspoint/epub_1/thumb_W120_H180.bmp");

  CHECK(Storage.writeFile("/.crosspoint/epub_1/thumb_226.bmp", "mid"));
  const auto best = network::resolveCoverAssetPath("/books/t.epub", books, nullptr, suffixes);
  CHECK(best.statusCode == 200);
  CHECK(best.resolvedPath == "/.crosspoint/epub_1/thumb_226.bmp");
}

TEST_CASE("asset read api never serves a zero-byte no-cover marker") {
  // generateThumbBmps writes empty thumbs for books without a cover image.
  Storage.reset();
  CHECK(Storage.writeFile("/.crosspoint/epub_2/thumb_W300_H450.bmp", ""));
  const std::vector<RecentBook> books = {
      {"/books/n.epub", "N", "A", "/.crosspoint/epub_2/thumb_[HEIGHT].bmp"},
  };
  const auto result = network::resolveCoverAssetPath("/books/n.epub", books, nullptr, {"W300_H450", "226"});
  CHECK(result.statusCode == 404);
  CHECK(result.body == "No cover available");
}

TEST_CASE("asset read api lists supported sleep images from canonical sleep directory") {
  Storage.reset();
  CHECK(Storage.writeFile("/sleep/first.bmp", "bmp"));
  CHECK(Storage.writeFile("/sleep/second.PNG", "png"));
  CHECK(Storage.writeFile("/sleep/pokedex/pikachu.bmp", "bmp"));
  CHECK(Storage.writeFile("/sleep/pokedex/party/party_demo.bmp", "bmp"));
  CHECK(Storage.writeFile("/sleep/.hidden.bmp", "hidden"));
  CHECK(Storage.writeFile("/sleep/notes.txt", "txt"));
  CHECK(Storage.writeFile("/.sleep/third.jpg", "jpg"));
  CHECK(Storage.writeFile("/sleep.jpg", "jpg"));

  const String json = network::buildSleepImagesJson();

  CHECK(json.indexOf("\"path\":\"/sleep/first.bmp\"") != -1);
  CHECK(json.indexOf("\"path\":\"/sleep/second.PNG\"") != -1);
  CHECK(json.indexOf("\"path\":\"/sleep/pokedex/pikachu.bmp\"") != -1);
  CHECK(json.indexOf("\"path\":\"/sleep/pokedex/party/party_demo.bmp\"") != -1);
  CHECK(json.indexOf("\"path\":\"/.sleep/third.jpg\"") == -1);
  CHECK(json.indexOf("\"path\":\"/sleep.jpg\"") == -1);
  CHECK(json.indexOf(".hidden.bmp") == -1);
  CHECK(json.indexOf("notes.txt") == -1);
}
