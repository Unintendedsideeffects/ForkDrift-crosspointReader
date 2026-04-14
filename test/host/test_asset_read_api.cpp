#include "doctest/doctest.h"

#include "network/AssetReadApi.h"
#include "test/mock/HalStorage.h"

#include <cstring>
#include <string>
#include <vector>

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

TEST_CASE("asset read api lists supported sleep images from both sleep directories") {
  Storage.reset();
  CHECK(Storage.writeFile("/sleep/first.bmp", "bmp"));
  CHECK(Storage.writeFile("/sleep/second.PNG", "png"));
  CHECK(Storage.writeFile("/sleep/.hidden.bmp", "hidden"));
  CHECK(Storage.writeFile("/sleep/notes.txt", "txt"));
  CHECK(Storage.writeFile("/.sleep/third.jpg", "jpg"));

  const String json = network::buildSleepImagesJson();

  CHECK(json.indexOf("\"path\":\"/sleep/first.bmp\"") != -1);
  CHECK(json.indexOf("\"path\":\"/sleep/second.PNG\"") != -1);
  CHECK(json.indexOf("\"path\":\"/.sleep/third.jpg\"") != -1);
  CHECK(json.indexOf(".hidden.bmp") == -1);
  CHECK(json.indexOf("notes.txt") == -1);
}
