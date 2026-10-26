#include "Epub/SectionCachePolicy.h"
#include "doctest/doctest.h"

TEST_CASE("a section that dropped any word under heap pressure is never cached") {
  using namespace section_cache;

  CHECK(evaluateBuiltSection(0) == BuiltSectionAction::Cache);
  // Device walk 2026-10-26: one dropped block cached "the great new of your to a a".
  CHECK(evaluateBuiltSection(1) == BuiltSectionAction::RefuseTruncated);
  CHECK(evaluateBuiltSection(7) == BuiltSectionAction::RefuseTruncated);
}
