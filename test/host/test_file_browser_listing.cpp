#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {

size_t countThenRewind(HalFile& root) {
  size_t entryCount = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    ++entryCount;
  }
  root.rewindDirectory();
  return entryCount;
}

std::vector<std::string> fillLikeLoadFiles(HalFile& root, size_t entryCount) {
  std::vector<std::string> files;
  files.reserve(entryCount);
  const size_t reserved = files.capacity();
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.getName(name, sizeof(name))) {
      files.emplace_back(name);
    }
  }
  REQUIRE(files.capacity() == reserved);
  REQUIRE(files.capacity() >= files.size());
  return files;
}

}  // namespace

TEST_CASE("two-pass directory listing reserves before fill") {
  Storage.reset();
  REQUIRE(Storage.writeFile("/books/a.epub", "a"));
  REQUIRE(Storage.writeFile("/books/b.epub", "b"));
  REQUIRE(Storage.writeFile("/books/c.epub", "c"));

  auto root = Storage.open("/books");
  REQUIRE(root);
  REQUIRE(root.isDirectory());

  const size_t firstWalk = countThenRewind(root);
  CHECK(firstWalk == 3);

  size_t secondWalk = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    ++secondWalk;
  }
  CHECK(secondWalk == firstWalk);
  root.rewindDirectory();

  const auto files = fillLikeLoadFiles(root, firstWalk);
  CHECK(files.size() == 3);
  CHECK(files.capacity() >= 3);
}

TEST_CASE("empty directory does not reserve leftover capacity") {
  Storage.reset();
  REQUIRE(Storage.mkdir("/empty"));

  auto root = Storage.open("/empty");
  REQUIRE(root);
  REQUIRE(root.isDirectory());

  std::vector<std::string> files;
  const size_t entryCount = countThenRewind(root);
  CHECK(entryCount == 0);
  files.reserve(entryCount);
  CHECK(files.empty());
}

TEST_CASE("open-fail listing leaves files empty without reserve") {
  Storage.reset();
  auto root = Storage.open("/missing");
  CHECK(!root);
  std::vector<std::string> files;
  CHECK(files.empty());
  CHECK(files.capacity() == 0);
}
