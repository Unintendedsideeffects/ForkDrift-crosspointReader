#include "doctest/doctest.h"

#include <vector>

#include "network/FileMutationApi.h"
#include "test/mock/HalStorage.h"

TEST_CASE("file mutation api creates folders in normalized parent path") {
  Storage.reset();

  const auto result = network::createFolder("/books/", "new-folder");

  REQUIRE(result.ok());
  CHECK(Storage.exists("/books/new-folder"));
}

TEST_CASE("file mutation api renames files within parent folder") {
  Storage.reset();
  REQUIRE(Storage.writeFile("/books/original.epub", "data"));
  std::vector<String> invalidated;

  const auto result = network::renameFile("/books/original.epub", "renamed.epub", true,
                                          [&](const String& path) { invalidated.push_back(path); });

  REQUIRE(result.ok());
  CHECK(Storage.exists("/books/renamed.epub"));
  CHECK(!Storage.exists("/books/original.epub"));
  REQUIRE(invalidated.size() == 1);
  CHECK(invalidated[0] == "/books/original.epub");
}

TEST_CASE("file mutation api moves files into destination folder") {
  Storage.reset();
  REQUIRE(Storage.writeFile("/books/demo.epub", "data"));
  REQUIRE(Storage.mkdir("/archive"));

  const auto result = network::moveFile("/books/demo.epub", "/archive", nullptr);

  REQUIRE(result.ok());
  CHECK(Storage.exists("/archive/demo.epub"));
  CHECK(!Storage.exists("/books/demo.epub"));
}

TEST_CASE("file mutation api deletes multiple files") {
  Storage.reset();
  REQUIRE(Storage.writeFile("/books/a.epub", "a"));
  REQUIRE(Storage.writeFile("/books/b.epub", "b"));
  std::vector<String> invalidated;

  const auto result = network::deletePaths({"/books/a.epub", "/books/b.epub"},
                                           [&](const String& path) { invalidated.push_back(path); });

  REQUIRE(result.ok());
  CHECK(!Storage.exists("/books/a.epub"));
  CHECK(!Storage.exists("/books/b.epub"));
  CHECK(invalidated.size() == 2);
}

TEST_CASE("file mutation api rejects deleting non-empty folder") {
  Storage.reset();
  REQUIRE(Storage.writeFile("/docs/nested/file.txt", "x"));

  const auto result = network::deletePaths({"/docs/nested"}, nullptr);

  CHECK(result.statusCode == 500);
  CHECK(result.body.indexOf("folder not empty") >= 0);
  CHECK(Storage.exists("/docs/nested"));
}
