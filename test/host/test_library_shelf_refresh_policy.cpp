#include "doctest/doctest.h"
#include "network/background/LibraryShelfRefreshPolicy.h"

TEST_CASE("library shelf admits measured Always-idle Home") {
  using namespace library_shelf;

  CHECK(kMinFreeBytes == 38000);

  CHECK(evaluate({39000, false}) == RefreshAction::Refresh);
  CHECK(evaluate({37000, false}) == RefreshAction::SkipLuxury);
  CHECK(evaluate({kMinFreeBytes, false}) == RefreshAction::Refresh);
  CHECK(evaluate({kMinFreeBytes - 1, false}) == RefreshAction::SkipLuxury);

  CHECK(evaluate({20000, true}) == RefreshAction::PauseHttpThenRetry);
  CHECK(evaluate({20000, false}) == RefreshAction::SkipLuxury);
  CHECK(evaluate({39000, true}) == RefreshAction::Refresh);
}

TEST_CASE("library shelf 84 KB floor is not required") {
  CHECK(library_shelf::evaluate({39000, false}) == library_shelf::RefreshAction::Refresh);
  CHECK(library_shelf::kMinFreeBytes < 84000);
}

TEST_CASE("library shelf caches authentication failures across Home re-entry") {
  using namespace library_shelf;

  CHECK_FALSE(skipAfterAuthFailure(1700000000, 0, true));
  CHECK(skipAfterAuthFailure(1700000000, 1700000000, true));
  CHECK(skipAfterAuthFailure(1700000000 + kAuthBackoffSeconds - 1, 1700000000, true));
  CHECK_FALSE(skipAfterAuthFailure(1700000000 + kAuthBackoffSeconds, 1700000000, true));
  CHECK(skipAfterAuthFailure(0, 1, false));
  CHECK_FALSE(skipAfterAuthFailure(0, 0, false));
}
