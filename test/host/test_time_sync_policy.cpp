#include "doctest/doctest.h"
#include "util/TerminusRefreshPolicy.h"
#include "util/TimeSyncPolicy.h"
#include "util/WallClockInterval.h"

// Regression: the device synced its clock once per DAY and drifted visibly in
// between. TimeSync::loop() polls every 15 minutes, which made the cadence look
// right, but shouldSync()'s epoch gate used a 23-hour floor — the poll interval
// and the sync interval were different numbers and only the poll was visible.

TEST_CASE("clock resync floor is the documented one hour, not the old 23 hours") {
  CHECK(timesync::kMinResyncIntervalS == 3600);
  // Guards the actual regression: 23 h would make every in-day poll a no-op.
  CHECK(timesync::kMinResyncIntervalS < 23UL * 60UL * 60UL);
}

TEST_CASE("clock resync is due when never synced") { CHECK(timesync::resyncDue(1'700'000'000U, 0U)); }

TEST_CASE("clock resync respects the interval boundary") {
  constexpr uint32_t last = 1'700'000'000U;
  CHECK_FALSE(timesync::resyncDue(last, last));
  CHECK_FALSE(timesync::resyncDue(last + 3599U, last));
  CHECK(timesync::resyncDue(last + 3600U, last));
  CHECK(timesync::resyncDue(last + 7200U, last));
}

TEST_CASE("clock resync treats a future last-sync stamp as due") {
  // The clock moved backwards (manual set, or a bad persisted seed). Without
  // this branch the unsigned subtraction underflows to a huge interval and the
  // device refuses to resync, stranding it on the wrong time.
  constexpr uint32_t last = 1'700'000'000U;
  CHECK(timesync::resyncDue(last - 1U, last));
  CHECK(timesync::resyncDue(0U, last));
}

TEST_CASE("clock resync honours an explicit interval override") {
  constexpr uint32_t last = 1'700'000'000U;
  CHECK_FALSE(timesync::resyncDue(last + 899U, last, 900U));
  CHECK(timesync::resyncDue(last + 900U, last, 900U));
}

// ── Shared wall-clock interval gate ─────────────────────────────────────────
// Used by the clock resync, the Terminus image refresh, and (next) KOReader
// sync. One implementation so the backwards-clock underflow is fixed once.

TEST_CASE("wall-clock interval treats never-run as due") {
  CHECK(wallclock::intervalElapsed(1'700'000'000U, 0U, 900U));
}

TEST_CASE("wall-clock interval is inclusive at the boundary") {
  constexpr uint32_t last = 1'700'000'000U;
  CHECK_FALSE(wallclock::intervalElapsed(last + 899U, last, 900U));
  CHECK(wallclock::intervalElapsed(last + 900U, last, 900U));
}

TEST_CASE("wall-clock interval does not underflow when the clock moves backwards") {
  // The bug this guards: unsigned (now - last) wraps to ~UINT32_MAX and reads
  // as "not due", wedging a periodic job until real time catches up.
  constexpr uint32_t last = 1'700'000'000U;
  CHECK(wallclock::intervalElapsed(last - 1U, last, 900U));
  CHECK(wallclock::intervalElapsed(1U, last, 900U));
}

TEST_CASE("clock-usable threshold rejects an unset clock") {
  CHECK_FALSE(wallclock::clockUsable(0U));
  CHECK_FALSE(wallclock::clockUsable(wallclock::kClockSetEpochThreshold));
  CHECK(wallclock::clockUsable(wallclock::kClockSetEpochThreshold + 1U));
}

TEST_CASE("Terminus refresh policy follows the server interval after success") {
  constexpr uint32_t last = 1'700'000'000U;
  CHECK_FALSE(terminus_refresh::refreshDue(last + 899U, last, true, 900U));
  CHECK(terminus_refresh::refreshDue(last + 900U, last, true, 900U));
  CHECK(terminus_refresh::refreshDue(last + 20U, last, true, 20U));
}

TEST_CASE("Terminus refresh policy retries failed fetches sooner") {
  constexpr uint32_t last = 1'700'000'000U;
  CHECK_FALSE(terminus_refresh::refreshDue(last + 59U, last, false, 900U));
  CHECK(terminus_refresh::refreshDue(last + 60U, last, false, 900U));
}

TEST_CASE("Terminus server interval is bounded against request storms and stale dashboards") {
  CHECK(terminus_refresh::normalizeServerInterval(0U) == terminus_refresh::kMinServerIntervalS);
  CHECK(terminus_refresh::normalizeServerInterval(20U) == 20U);
  CHECK(terminus_refresh::normalizeServerInterval(100'000U) == terminus_refresh::kMaxServerIntervalS);
}
