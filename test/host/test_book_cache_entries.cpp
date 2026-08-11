#include <iterator>

#include "doctest/doctest.h"
#include "lib/Epub/Epub/BookCacheEntries.h"

// Epub::clearRenderCache() deletes every entry under a book's cache directory
// that this predicate does not claim. A store that stops being recognised here
// is deleted silently the next time the user re-uploads the book, so these
// cases exist to make that failure loud.

TEST_CASE("book cache: reader-owned entries survive invalidation") {
  CHECK(book_cache::isUserStateEntry("progress.bin"));
  CHECK(book_cache::isUserStateEntry("annotations.bin"));
  CHECK(book_cache::isUserStateEntry("book_settings.json"));
  CHECK(book_cache::isUserStateEntry("pokemon.json"));
}

TEST_CASE("book cache: derived entries are not protected") {
  // Everything regenerable from the book file must be clearable, or a changed
  // book keeps rendering from a stale layout.
  CHECK_FALSE(book_cache::isUserStateEntry("book.bin"));
  CHECK_FALSE(book_cache::isUserStateEntry("index.bin"));
  CHECK_FALSE(book_cache::isUserStateEntry("sections"));
  CHECK_FALSE(book_cache::isUserStateEntry("css_rules.cache"));
  CHECK_FALSE(book_cache::isUserStateEntry("toc.ncx"));
  CHECK_FALSE(book_cache::isUserStateEntry("toc.nav"));
  CHECK_FALSE(book_cache::isUserStateEntry(".cover.jpg"));
  CHECK_FALSE(book_cache::isUserStateEntry("thumb_120.bmp"));
}

TEST_CASE("book cache: matching is exact") {
  // Prefix or suffix matching would protect scratch files like progress.bin.tmp
  // and, worse, could be tricked into protecting derived data.
  CHECK_FALSE(book_cache::isUserStateEntry("progress.bin.tmp"));
  CHECK_FALSE(book_cache::isUserStateEntry("progress"));
  CHECK_FALSE(book_cache::isUserStateEntry("my_progress.bin"));
  CHECK_FALSE(book_cache::isUserStateEntry(""));
  CHECK_FALSE(book_cache::isUserStateEntry(nullptr));
}

TEST_CASE("book cache: the protected set is pinned") {
  // Deliberate tripwire. If you added a per-book store, add it to
  // kUserStateEntries and bump this count; if this fires without you having
  // added one, a store just lost its protection.
  CHECK(std::size(book_cache::kUserStateEntries) == 4);
}
