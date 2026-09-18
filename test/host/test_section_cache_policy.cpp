#include "Epub/SectionCachePolicy.h"
#include "doctest/doctest.h"

TEST_CASE("empty section cache keeps text and skipped-image chapters") {
  using namespace section_cache;

  CHECK(evaluateEmptySection({1, 0, 0}) == EmptySectionAction::Cache);
  CHECK(evaluateEmptySection({4, 2, 0}) == EmptySectionAction::Cache);
  CHECK(evaluateEmptySection({0, 0, 0}) == EmptySectionAction::Cache);
  CHECK(evaluateEmptySection({0, 1, 1}) == EmptySectionAction::Cache);
  CHECK(evaluateEmptySection({0, 1, 0}) == EmptySectionAction::RefuseOccupancy);
}
