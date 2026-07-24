#include <cstdio>
#include <string>

#include "doctest/doctest.h"
#include "src/util/HighlightExporter.h"

namespace {

// A book with two highlights (one multi-line, one with quotes) and one bookmark.
highlight_export::BookExport sampleBook() {
  highlight_export::BookExport book;
  book.title = "Site Reliability Engineering";
  book.author = "Betsy Beyer";
  book.path = "/Books/sre.epub";
#if ENABLE_ANNOTATIONS
  Annotation a;
  a.spineIndex = 16;
  a.text = "Hope is not a strategy.";
  book.highlights.push_back(a);

  Annotation b;
  b.spineIndex = 17;
  b.text = "Line1\nLine2 with \"quotes\"";
  book.highlights.push_back(b);
#endif
#if ENABLE_BOOKMARKS
  Bookmark bm{};
  bm.spineIndex = 5;
  bm.progress = 0.37f;
  bm.timestamp = 1700000000;  // fixed for deterministic My Clippings / KOReader dates
  std::snprintf(bm.chapterTitle, sizeof(bm.chapterTitle), "%s", "Testing for Reliability");
  book.bookmarks.push_back(bm);
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
