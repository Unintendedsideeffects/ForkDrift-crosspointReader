#include <cstdio>
#include <string>

#include "doctest/doctest.h"
#include "src/util/HighlightExporter.h"
#include "test/mock/HalStorage.h"

namespace {

// A book with two highlights (one multi-line, one with quotes) and one bookmark.
highlight_export::BookExport sampleBook() {
  highlight_export::BookExport book;
  book.title = "Site Reliability Engineering";
  book.author = "Betsy Beyer";
  book.path = "/Books/sre.epub";
#if ENABLE_ANNOTATIONS
  static std::vector<Annotation> s_highlights;
  s_highlights.clear();
  Annotation a;
  a.spineIndex = 16;
  a.text = "Hope is not a strategy.";
  s_highlights.push_back(a);

  Annotation b;
  b.spineIndex = 17;
  b.text = "Line1\nLine2 with \"quotes\"";
  s_highlights.push_back(b);
  book.highlights = &s_highlights;
#endif
#if ENABLE_BOOKMARKS
  static std::vector<Bookmark> s_bookmarks;
  s_bookmarks.clear();
  Bookmark bm{};
  bm.spineIndex = 5;
  bm.progress = 0.37f;
  bm.timestamp = 1700000000;  // fixed for deterministic My Clippings / KOReader dates
  std::snprintf(bm.chapterTitle, sizeof(bm.chapterTitle), "%s", "Testing for Reliability");
  s_bookmarks.push_back(bm);
  book.bookmarks = &s_bookmarks;
#endif
  return book;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("hasContent is false only when there is nothing to export") {
  highlight_export::BookExport empty;
  empty.title = "Empty";
  CHECK_FALSE(highlight_export::hasContent(empty));
  CHECK(highlight_export::hasContent(sampleBook()));
}

TEST_CASE("markdown export has title, blockquoted highlights, and bookmarks") {
  const std::string md = highlight_export::serializeMarkdown(sampleBook());
  CHECK(contains(md, "# Site Reliability Engineering"));
  CHECK(contains(md, "*Betsy Beyer*"));
#if ENABLE_ANNOTATIONS
  CHECK(contains(md, "## Highlights"));
  CHECK(contains(md, "> Hope is not a strategy."));
  // Multi-line highlights keep every line quoted.
  CHECK(contains(md, "> Line1\n> Line2 with \"quotes\""));
#endif
#if ENABLE_BOOKMARKS
  CHECK(contains(md, "## Bookmarks"));
  CHECK(contains(md, "Testing for Reliability"));
  CHECK(contains(md, "37%"));
#endif
}

TEST_CASE("my clippings export uses the Kindle header + separator") {
  const std::string mc = highlight_export::serializeMyClippings(sampleBook());
  CHECK(contains(mc, "Site Reliability Engineering (Betsy Beyer)"));
  CHECK(contains(mc, "=========="));
#if ENABLE_ANNOTATIONS
  CHECK(contains(mc, "- Your Highlight | Location"));
  CHECK(contains(mc, "Hope is not a strategy."));
#endif
#if ENABLE_BOOKMARKS
  CHECK(contains(mc, "- Your Bookmark | Location"));
#endif
}

TEST_CASE("koreader export is a lua table with escaped strings") {
  const std::string lua = highlight_export::serializeKoreader(sampleBook());
  CHECK(contains(lua, "return {"));
  CHECK(contains(lua, "}\n"));
#if ENABLE_ANNOTATIONS
  CHECK(contains(lua, "[\"highlight\"]"));
  CHECK(contains(lua, "Hope is not a strategy."));
  // Embedded quotes and newlines must be Lua-escaped so the file stays parseable.
  CHECK(contains(lua, "\\\"quotes\\\""));
  CHECK(contains(lua, "\\n"));
#endif
#if ENABLE_BOOKMARKS
  CHECK(contains(lua, "[\"bookmarks\"]"));
  CHECK(contains(lua, "[\"chapter\"] = \"Testing for Reliability\""));
#endif
}

#if ENABLE_ANNOTATIONS
TEST_CASE("testExportLargeBookStaysBounded") {
  highlight_export::BookExport book;
  book.title = "Large Book Test";
  book.author = "Scale Author";
  book.path = "/Books/large.epub";

  static std::vector<Annotation> s_largeHighlights;
  s_largeHighlights.clear();
  s_largeHighlights.reserve(200);

  std::string pattern = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string largeText;
  largeText.reserve(1024);
  while (largeText.size() < 1024) {
    largeText += pattern;
  }
  largeText.resize(1024);

  for (size_t i = 0; i < 200; ++i) {
    Annotation a;
    a.spineIndex = static_cast<uint16_t>(i);
    a.text = "START_" + std::to_string(i) + "_" + largeText.substr(0, 950) + "_END_" + std::to_string(i);
    s_largeHighlights.push_back(a);
  }
  book.highlights = &s_largeHighlights;

  const std::string md = highlight_export::serializeMarkdown(book);
  CHECK(!md.empty());
  CHECK(md.size() < 2 * 200 * 1024);
  CHECK(contains(md, "START_0_"));
  CHECK(contains(md, "END_199"));

  const std::string mc = highlight_export::serializeMyClippings(book);
  CHECK(!mc.empty());
  CHECK(mc.size() < 2 * 200 * 1024);
  CHECK(contains(mc, "START_0_"));
  CHECK(contains(mc, "END_199"));

  const std::string lua = highlight_export::serializeKoreader(book);
  CHECK(!lua.empty());
  CHECK(lua.size() < 2 * 200 * 1024);
  CHECK(contains(lua, "START_0_"));
  CHECK(contains(lua, "END_199"));
}
#endif

TEST_CASE("KOReader sidecar export - absent sidecar is written") {
  Storage.reset();
  const auto book = sampleBook();
  std::string outPath;
  const auto status = highlight_export::exportBook(highlight_export::FORMAT_KOREADER, book, outPath);
  CHECK(status == highlight_export::ExportStatus::Ok);
  CHECK(outPath == "/Books/sre.sdr/metadata.epub.lua");
  CHECK(Storage.exists(outPath.c_str()));
  HalFile f;
  REQUIRE(Storage.openFileForRead("TEST", outPath.c_str(), f));
  char head[64] = {0};
  f.read(reinterpret_cast<uint8_t*>(head), sizeof(head) - 1);
  CHECK(std::string(head).find("-- exported by CrossPoint Reader") == 0);
}

TEST_CASE("KOReader sidecar export - our sidecar is overwritten") {
  Storage.reset();
  const auto book = sampleBook();
  const std::string sidecarPath = "/Books/sre.sdr/metadata.epub.lua";
  Storage.mkdir("/Books/sre.sdr");
  HalFile f;
  REQUIRE(Storage.openFileForWrite("TEST", sidecarPath.c_str(), f));
  const std::string oldContent = "-- exported by CrossPoint Reader\nreturn {\n  [\"old\"] = true\n}\n";
  f.write(reinterpret_cast<const uint8_t*>(oldContent.data()), oldContent.size());
  f.close();

  std::string outPath;
  const auto status = highlight_export::exportBook(highlight_export::FORMAT_KOREADER, book, outPath);
  CHECK(status == highlight_export::ExportStatus::Ok);
  CHECK(outPath == sidecarPath);
  REQUIRE(Storage.openFileForRead("TEST", sidecarPath.c_str(), f));
  // Read back and check it's replaced
  char readBuf[1024] = {0};
  const size_t rd = f.read(reinterpret_cast<uint8_t*>(readBuf), sizeof(readBuf) - 1);
  CHECK(rd > 0);
  CHECK(std::string(readBuf).find("Hope is not a strategy.") != std::string::npos);
}

TEST_CASE("KOReader sidecar export - foreign sidecar is preserved") {
  Storage.reset();
  const auto book = sampleBook();
  const std::string sidecarPath = "/Books/sre.sdr/metadata.epub.lua";
  Storage.mkdir("/Books/sre.sdr");
  HalFile f;
  REQUIRE(Storage.openFileForWrite("TEST", sidecarPath.c_str(), f));
  const std::string foreignContent = "return {\n  [\"doc_props\"] = {},\n  [\"percent_finished\"] = 0.42,\n}\n";
  f.write(reinterpret_cast<const uint8_t*>(foreignContent.data()), foreignContent.size());
  f.close();

  std::string outPath;
  const auto status = highlight_export::exportBook(highlight_export::FORMAT_KOREADER, book, outPath);
  CHECK(status == highlight_export::ExportStatus::SidecarNotOurs);
  CHECK(outPath.empty());

  REQUIRE(Storage.openFileForRead("TEST", sidecarPath.c_str(), f));
  char readBuf[1024] = {0};
  const size_t rd = f.read(reinterpret_cast<uint8_t*>(readBuf), sizeof(readBuf) - 1);
  CHECK(rd == foreignContent.size());
  CHECK(std::string(readBuf, rd) == foreignContent);
}

TEST_CASE("KOReader sidecar export - empty existing sidecar is written") {
  Storage.reset();
  const auto book = sampleBook();
  const std::string sidecarPath = "/Books/sre.sdr/metadata.epub.lua";
  Storage.mkdir("/Books/sre.sdr");
  HalFile f;
  REQUIRE(Storage.openFileForWrite("TEST", sidecarPath.c_str(), f));
  f.close();

  std::string outPath;
  const auto status = highlight_export::exportBook(highlight_export::FORMAT_KOREADER, book, outPath);
  CHECK(status == highlight_export::ExportStatus::Ok);
  CHECK(outPath == sidecarPath);
}
