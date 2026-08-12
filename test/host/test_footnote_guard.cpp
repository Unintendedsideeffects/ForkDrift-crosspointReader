#include "Epub/parsers/FootnoteGuard.h"
#include "doctest/doctest.h"

// Second occurrence of "unguarded container growth aborts the device" (the first was
// ParsedText's word vectors, see the OOM guard at ParsedText.cpp:264). This one was found
// on hardware: abort() -> __terminate inside endElement()'s push_back while building the
// first section of a footnote-dense book, largest free block 15348 bytes.

using footnote_guard::canAccept;

TEST_CASE("footnote guard: accepts freely while spare capacity remains") {
  // Below capacity push_back cannot allocate, so heap state is irrelevant and must not
  // be allowed to reject. Dropping footnotes on a healthy heap would be a silent
  // regression in a feature that works today.
  CHECK(canAccept(0, 8, 512, false));
  CHECK(canAccept(7, 8, 512, false));
}

TEST_CASE("footnote guard: a growth step needs heap headroom") {
  // size == capacity is exactly the push_back that reallocates.
  CHECK(canAccept(8, 8, 512, true));
  CHECK_FALSE(canAccept(8, 8, 512, false));
}

TEST_CASE("footnote guard: the hard cap wins over available heap") {
  // The cap bounds the worst case even on a healthy heap, so one pathological chapter
  // cannot consume the reading budget.
  CHECK_FALSE(canAccept(512, 1024, 512, true));
  CHECK_FALSE(canAccept(600, 1024, 512, true));
}

TEST_CASE("footnote guard: the entry just below the cap is still accepted") {
  // Off-by-one on the cap would silently drop one footnote per chapter forever.
  CHECK(canAccept(511, 1024, 512, true));
}

TEST_CASE("footnote guard: a zero cap disables collection entirely") { CHECK_FALSE(canAccept(0, 0, 0, true)); }

TEST_CASE("footnote guard: the very first insertion is a growth step") {
  // An empty vector has capacity 0, so the first push_back allocates and must be
  // subject to the heap check like any other growth.
  CHECK(canAccept(0, 0, 512, true));
  CHECK_FALSE(canAccept(0, 0, 512, false));
}
