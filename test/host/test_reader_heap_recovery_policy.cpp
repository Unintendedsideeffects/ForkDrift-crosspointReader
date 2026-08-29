#include "activities/reader/ReaderHeapRecoveryPolicy.h"
#include "doctest/doctest.h"

TEST_CASE("post-index recovery never restarts") {
  using namespace reader_heap_recovery;

  CHECK(kReclaimBelowLargestBlockBytes == 14 * 1024);
  CHECK(kMaxReclaimAttempts == 2);

  CHECK(decideAfterIndex(false, 0, 0) == AfterIndexAction::Continue);
  CHECK(decideAfterIndex(true, kReclaimBelowLargestBlockBytes, 0) == AfterIndexAction::Continue);
  CHECK(decideAfterIndex(true, 14836, 0) == AfterIndexAction::Continue);
  CHECK(decideAfterIndex(true, 16372, 0) == AfterIndexAction::Continue);

  CHECK(decideAfterIndex(true, kReclaimBelowLargestBlockBytes - 1, 0) == AfterIndexAction::Reclaim);
  CHECK(decideAfterIndex(true, 0, 1) == AfterIndexAction::Reclaim);
  CHECK(decideAfterIndex(true, 0, kMaxReclaimAttempts) == AfterIndexAction::StopRetrying);
  CHECK(decideAfterIndex(true, 0, 255) == AfterIndexAction::StopRetrying);
}
