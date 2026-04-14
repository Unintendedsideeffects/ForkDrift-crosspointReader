#include "doctest/doctest.h"

#include <ArduinoJson.h>

#include <string>

#include "network/FileReadApi.h"
#include "test/mock/HalStorage.h"

TEST_CASE("file read api builds file list json for visible entries") {
  Storage.reset();
  CHECK(Storage.writeFile("/books/novel.epub", "epub"));
  CHECK(Storage.writeFile("/books/readme.txt", "txt"));
  CHECK(Storage.writeFile("/books/.hidden", "hidden"));

  const auto result = network::buildFileListJson("/books", false);

  REQUIRE(result.ok());
  JsonDocument doc;
  REQUIRE(deserializeJson(doc, result.body.c_str()) == DeserializationError::Ok);
  REQUIRE(doc.is<JsonArray>());
  CHECK(doc.as<JsonArray>().size() == 2);
}

TEST_CASE("file read api rejects protected list paths") {
  const auto result = network::buildFileListJson("/.crosspoint", true);
  CHECK(result.statusCode == 403);
}

TEST_CASE("file read api resolves download metadata for epub files") {
  Storage.reset();
  CHECK(Storage.writeFile("/books/demo.epub", "epub-data"));

  const auto result = network::resolveDownload("/books/demo.epub");

  REQUIRE(result.ok());
  CHECK(result.contentType == "application/epub+zip");
  CHECK(result.filename == "demo.epub");
  CHECK(result.fileSize == 9);
  CHECK(result.normalizedPath == "/books/demo.epub");
}

TEST_CASE("file read api rejects directory downloads") {
  Storage.reset();
  CHECK(Storage.mkdir("/books"));

  const auto result = network::resolveDownload("/books");

  CHECK(result.statusCode == 400);
  CHECK(result.body == "Path is a directory");
}
