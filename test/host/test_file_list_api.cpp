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

TEST_CASE("file list api shows dot files when showHiddenFiles is true") {
  Storage.reset();
  CHECK(Storage.writeFile("/docs/visible.txt", "v"));
  CHECK(Storage.writeFile("/docs/.dot-file.txt", "h"));

  // Dotfiles visible when showHiddenFiles=true
  const auto withHidden = collectEntries("/docs", /*showHiddenFiles=*/true);
  // Dotfiles hidden when showHiddenFiles=false
  const auto withoutHidden = collectEntries("/docs", /*showHiddenFiles=*/false);

  REQUIRE(withHidden.size() == 2);
  REQUIRE(withoutHidden.size() == 1);
  CHECK(withoutHidden[0].name == "visible.txt");

  bool foundVisible = false;
  bool foundDot = false;
  for (const auto& e : withHidden) {
    if (e.name == "visible.txt") foundVisible = true;
    if (e.name == ".dot-file.txt") foundDot = true;
  }
  CHECK(foundVisible);
  CHECK(foundDot);
}

TEST_CASE("file list api excludes named protected web components regardless of showHiddenFiles") {
  Storage.reset();
  // Named protected items (not dot-prefixed) must always be hidden.
  // Dot-prefixed entries like .crosspoint appear when showHiddenFiles=true.
  CHECK(Storage.mkdir("/.crosspoint"));
  CHECK(Storage.writeFile("/.crosspoint/cache.bin", "data"));
  CHECK(Storage.writeFile("/books/novel.epub", "epub"));
  // XTCache is a named protected component (no dot prefix) - always hidden.
  CHECK(Storage.mkdir("/XTCache"));

  // With showHiddenFiles=false: both .crosspoint and XTCache hidden
  const auto entriesHidden = collectEntries("/", /*showHiddenFiles=*/false);
  for (const auto& e : entriesHidden) {
    CHECK(e.name != ".crosspoint");
    CHECK(e.name != "XTCache");
  }

  // With showHiddenFiles=true: .crosspoint visible, XTCache still hidden
  const auto entriesVisible = collectEntries("/", /*showHiddenFiles=*/true);
  bool foundDotCrosspoint = false;
  bool foundXTCache = false;
  for (const auto& e : entriesVisible) {
    if (e.name == ".crosspoint") foundDotCrosspoint = true;
    if (e.name == "XTCache") foundXTCache = true;
  }
  CHECK(foundDotCrosspoint);   // dotfiles show when showHiddenFiles=true
  CHECK(!foundXTCache);        // named protected components always hidden

  // At least books directory is visible in both modes
  bool foundBooks = false;
  for (const auto& e : entriesHidden) {
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
