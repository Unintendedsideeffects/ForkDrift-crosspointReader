#include "doctest/doctest.h"

#include <string>
#include <vector>

#include "network/FileListApi.h"
#include "test/mock/HalStorage.h"

static std::vector<network::DirEntry> collectEntries(const char* path, bool showHidden) {
  std::vector<network::DirEntry> entries;
  network::scanDirectory(path, showHidden, [&](const network::DirEntry& e) { entries.push_back(e); });
  return entries;
}

TEST_CASE("file list api returns empty for missing directory") {
  Storage.reset();
  const auto entries = collectEntries("/missing", false);
  CHECK(entries.empty());
}

TEST_CASE("file list api lists files excluding hidden entries by default") {
  Storage.reset();
  CHECK(Storage.writeFile("/books/novel.epub", "epub"));
  CHECK(Storage.writeFile("/books/readme.txt", "txt"));
  CHECK(Storage.writeFile("/books/.hidden", "hidden"));

  const auto entries = collectEntries("/books", /*showHiddenFiles=*/false);

  REQUIRE(entries.size() == 2);
  bool foundEpub = false;
  bool foundTxt = false;
  for (const auto& e : entries) {
    if (e.name == "novel.epub") { foundEpub = true; CHECK(e.isEpub); CHECK(!e.isDirectory); }
    if (e.name == "readme.txt") { foundTxt = true; CHECK(!e.isEpub); CHECK(!e.isDirectory); }
  }
  CHECK(foundEpub);
  CHECK(foundTxt);
}

TEST_CASE("file list api dot files are always excluded via isProtectedWebComponent") {
  // Note: isProtectedWebComponent() returns true for anything starting with '.', so
  // dot files are ALWAYS excluded regardless of showHiddenFiles. The showHiddenFiles
  // flag provides a secondary dot-file check but isProtectedWebComponent is the gate.
  Storage.reset();
  CHECK(Storage.writeFile("/docs/visible.txt", "v"));
  CHECK(Storage.writeFile("/docs/.dot-file.txt", "h"));

  // Dot files are excluded in both modes
  const auto withHidden = collectEntries("/docs", /*showHiddenFiles=*/true);
  const auto withoutHidden = collectEntries("/docs", /*showHiddenFiles=*/false);

  CHECK(withHidden.size() == 1);
  CHECK(withoutHidden.size() == 1);
  CHECK(withHidden[0].name == "visible.txt");
  CHECK(withoutHidden[0].name == "visible.txt");
}

TEST_CASE("file list api excludes protected web components") {
  Storage.reset();
  // .crosspoint is a protected directory that should never appear in file listings
  CHECK(Storage.mkdir("/.crosspoint"));
  CHECK(Storage.writeFile("/.crosspoint/cache.bin", "data"));
  CHECK(Storage.writeFile("/books/novel.epub", "epub"));

  const auto entries = collectEntries("/", /*showHiddenFiles=*/true);

  for (const auto& e : entries) {
    CHECK(e.name != ".crosspoint");
  }
  // At least books directory is visible
  bool foundBooks = false;
  for (const auto& e : entries) {
    if (e.name == "books") { foundBooks = true; CHECK(e.isDirectory); }
  }
  CHECK(foundBooks);
}

TEST_CASE("file list api correctly identifies epub files") {
  Storage.reset();
  CHECK(Storage.writeFile("/shelf/book.epub", "epub"));
  CHECK(Storage.writeFile("/shelf/book.EPUB", "epub"));
  CHECK(Storage.writeFile("/shelf/doc.pdf", "pdf"));

  const auto entries = collectEntries("/shelf", false);

  REQUIRE(entries.size() == 3);
  for (const auto& e : entries) {
    if (e.name == "book.epub" || e.name == "book.EPUB") {
      CHECK(e.isEpub);
    } else {
      CHECK(!e.isEpub);
    }
  }
}

TEST_CASE("file list api reports directory sizes as zero") {
  Storage.reset();
  CHECK(Storage.writeFile("/root/file.txt", "content"));
  CHECK(Storage.mkdir("/root/subdir"));

  const auto entries = collectEntries("/root", false);

  for (const auto& e : entries) {
    if (e.isDirectory) {
      CHECK(e.size == 0);
      CHECK(!e.isEpub);
    }
  }
}
