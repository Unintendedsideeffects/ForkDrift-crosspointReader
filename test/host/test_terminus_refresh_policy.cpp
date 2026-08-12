#include "doctest/doctest.h"
#include "util/TerminusRefreshPolicy.h"

// The monotonic path exists because the epoch path cannot pace a device with no usable
// wall clock: trmnlLastAttemptEpoch can only be written when the clock is usable, so it
// stays 0 forever and "due if we have no timestamp" answers true on every background
// server start. See docs/FINDINGS.md 2026-08-12T20:30Z, defect 4.

using terminus_refresh::effectiveIntervalS;
using terminus_refresh::kDefaultIntervalS;
using terminus_refresh::kFailureRetryIntervalS;
using terminus_refresh::kMaxServerIntervalS;
using terminus_refresh::kMinServerIntervalS;
using terminus_refresh::refreshDueMonotonic;

TEST_CASE("terminus interval: a failure paces off the retry interval, not the server's") {
  CHECK(effectiveIntervalS(false, 900) == kFailureRetryIntervalS);
  CHECK(effectiveIntervalS(true, 900) == 900);
}

TEST_CASE("terminus interval: a hostile server value is clamped both ways") {
  // The floor is what stops a bad server value turning the main loop into a request storm.
  CHECK(effectiveIntervalS(true, 0) == kMinServerIntervalS);
  CHECK(effectiveIntervalS(true, 1) == kMinServerIntervalS);
  CHECK(effectiveIntervalS(true, kMaxServerIntervalS + 1) == kMaxServerIntervalS);
}

TEST_CASE("terminus monotonic: the first attempt of a boot is always due") {
  CHECK(refreshDueMonotonic(0, 0, /*haveAttempted=*/false, false, kDefaultIntervalS));
  CHECK(refreshDueMonotonic(5'000'000, 0, /*haveAttempted=*/false, true, kDefaultIntervalS));
}

TEST_CASE("terminus monotonic: a completed attempt is NOT immediately due again") {
  // The regression: without this the device refetched on every single server start.
  CHECK_FALSE(refreshDueMonotonic(1000, 1000, true, true, 900));
  CHECK_FALSE(refreshDueMonotonic(1000, 1000, true, false, 900));
}

TEST_CASE("terminus monotonic: a success waits the server interval") {
  const uint32_t last = 1'000'000;
  CHECK_FALSE(refreshDueMonotonic(last + 899'000, last, true, true, 900));
  CHECK(refreshDueMonotonic(last + 900'000, last, true, true, 900));
}

TEST_CASE("terminus monotonic: a failure waits only the retry interval") {
  const uint32_t last = 1'000'000;
  const uint32_t retryMs = kFailureRetryIntervalS * 1000UL;
  CHECK_FALSE(refreshDueMonotonic(last + retryMs - 1, last, true, false, 900));
  CHECK(refreshDueMonotonic(last + retryMs, last, true, false, 900));
  // ...and a failure must not be held back by the (much longer) server interval.
  CHECK(refreshDueMonotonic(last + retryMs, last, true, false, 86400));
}

TEST_CASE("terminus monotonic: survives the millis() wrap at 2^32") {
  // The attempt happened just before the counter wrapped; "now" is just after. An
  // unsigned comparison would answer "not due" for another 49 days.
  const uint32_t last = 0xFFFFFF00u;
  const uint32_t now = last + 120'000u;  // wraps
  CHECK(now < last);                     // guard: this really is a wrapped pair
  CHECK(refreshDueMonotonic(now, last, true, false, 900));
  // And immediately across the wrap it is still correctly not due.
  CHECK_FALSE(refreshDueMonotonic(last + 1000u, last, true, false, 900));
}
