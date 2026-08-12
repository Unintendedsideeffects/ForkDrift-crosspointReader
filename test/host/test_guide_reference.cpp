#include "Epub/parsers/GuideReference.h"
#include "doctest/doctest.h"

// This table decides where a book opens. Getting it wrong is not cosmetic: the
// reader jumps to an arbitrary chapter every single time the book is opened.

using guide_reference::classify;
using guide_reference::Slot;

TEST_CASE("guide reference: only \"start\" designates a reading location") {
  CHECK(classify("start", false, false) == Slot::StartLocation);
}

TEST_CASE("guide reference: \"text\" is ignored") {
  // The regression. EPUB 2 guides tag every content file as type="text", so
  // accepting it made the *last* such entry win and the book opened there.
  CHECK(classify("text", false, false) == Slot::Ignore);
  CHECK(classify("text", true, false) == Slot::Ignore);
}

TEST_CASE("guide reference: \"start\" is accepted with no prior text reference") {
  // The other half of the regression: the old condition required a text
  // reference to already exist, so a guide carrying only "start" -- the one
  // unambiguous marker -- was discarded entirely.
  CHECK(classify("start", false, false) == Slot::StartLocation);
}

TEST_CASE("guide reference: the first \"start\" wins") { CHECK(classify("start", true, false) == Slot::Ignore); }

TEST_CASE("guide reference: cover types map to the cover slot, first wins") {
  CHECK(classify("cover", false, false) == Slot::CoverPage);
  CHECK(classify("cover-page", false, false) == Slot::CoverPage);
  CHECK(classify("cover", false, true) == Slot::Ignore);
  CHECK(classify("cover-page", false, true) == Slot::Ignore);
}

TEST_CASE("guide reference: the two slots do not interfere") {
  // A recorded cover must not block a start location, or vice versa.
  CHECK(classify("start", false, true) == Slot::StartLocation);
  CHECK(classify("cover", true, false) == Slot::CoverPage);
}

TEST_CASE("guide reference: unknown and empty types are ignored") {
  CHECK(classify("toc", false, false) == Slot::Ignore);
  CHECK(classify("copyright-page", false, false) == Slot::Ignore);
  CHECK(classify("", false, false) == Slot::Ignore);
  // Matching is exact -- no prefix or substring leniency.
  CHECK(classify("started", false, false) == Slot::Ignore);
  CHECK(classify("cover-image", false, false) == Slot::Ignore);
}
