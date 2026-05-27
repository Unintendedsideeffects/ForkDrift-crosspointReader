#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "lib/Serialization/Serialization.h"
#include "src/BookmarkStore.h"
#include "test/mock/HalStorage.h"

namespace {
constexpr char kBookmarkDir[] = "/.crosspoint/bookmarks";

std::string bookmarkPathFor(const std::string& bookPath, const std::string& bookType) {
  const uint32_t crc = static_cast<uint32_t>(std::hash<std::string>{}(bookPath));
  return std::string(kBookmarkDir) + "/" + bookType + "_" + std::to_string(crc) + ".bin";
}

void writeBookmarkRecord(FsFile& file, uint16_t spineIndex, float progress, const char* chapterTitle) {
  serialization::writePod(file, spineIndex);
  serialization::writePod(file, progress);
  serialization::writePod(file, static_cast<uint32_t>(0));

  char title[BOOKMARK_CHAPTER_TITLE_MAX] = {0};
  std::snprintf(title, sizeof(title), "%s", chapterTitle);
  file.write(reinterpret_cast<const uint8_t*>(title), sizeof(title));
}

void writeBookmarkFile(const std::string& path, uint8_t version, bool legacyCount) {
  FsFile file;
  CHECK(Storage.openFileForWrite("TST", path, file));
  serialization::writePod(file, version);
  if (legacyCount) {
    serialization::writePod(file, static_cast<uint8_t>(1));
  } else {
    serialization::writePod(file, static_cast<uint16_t>(1));
  }
  serialization::writeString(file, std::string("Dune"));
  serialization::writeString(file, std::string("Frank Herbert"));
  serialization::writeString(file, std::string("/books/dune.epub"));
  writeBookmarkRecord(file, 2, 0.25f, "Arrakis");
  file.close();
}
}  // namespace

TEST_CASE("BookmarkStore loads current v3 bookmark files") {
  Storage.reset();
  Storage.writeFile("/books/dune.epub", "book");
  const std::string storePath = bookmarkPathFor("/books/dune.epub", "epub");
  writeBookmarkFile(storePath, 3, false);

  auto& store = BookmarkStore::getInstance();
  CHECK(store.loadForBook("/books/dune.epub", "Dune", "Frank Herbert", "epub"));
  REQUIRE(store.getBookmarks().size() == 1);
  CHECK(store.getBookmarks()[0].spineIndex == 2);
  CHECK(store.getBookmarks()[0].progress == doctest::Approx(0.25f));
  CHECK(std::string(store.getBookmarks()[0].chapterTitle) == "Arrakis");
  store.unload();
}

TEST_CASE("BookmarkStore rejects legacy v2 bookmark files instead of migrating them") {
  Storage.reset();
  Storage.writeFile("/books/dune.epub", "book");
  const std::string storePath = bookmarkPathFor("/books/dune.epub", "epub");
  writeBookmarkFile(storePath, 2, true);

  auto& store = BookmarkStore::getInstance();
  CHECK_FALSE(store.loadForBook("/books/dune.epub", "Dune", "Frank Herbert", "epub"));
  CHECK(store.getBookmarks().empty());

  FsFile file;
  REQUIRE(Storage.openFileForRead("TST", storePath, file));
  uint8_t version = 0;
  serialization::readPod(file, version);
  CHECK(version == 2);
  store.unload();
}

TEST_CASE("BookmarkStore listing skips legacy bookmark files") {
  Storage.reset();
  Storage.writeFile("/books/dune.epub", "book");
  writeBookmarkFile(bookmarkPathFor("/books/dune.epub", "epub"), 2, true);

  std::vector<BookmarkedBookEntry> entries;
  CHECK(BookmarkStore::getAllBookmarkedBooks(entries));
  CHECK(entries.empty());
}
