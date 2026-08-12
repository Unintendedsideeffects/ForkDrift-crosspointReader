#pragma once

#include <cstdint>

#include "util/WallClockInterval.h"

namespace terminus_refresh {

// Terminus defaults devices to 900 seconds, but /api/display can return a
// device-specific refresh_rate. Keep a small safety floor so a bad server value
// cannot turn the firmware main loop into an HTTP request storm.
constexpr uint32_t kDefaultIntervalS = 15UL * 60UL;
constexpr uint32_t kFailureRetryIntervalS = 60UL;
constexpr uint32_t kMinServerIntervalS = 15UL;
constexpr uint32_t kMaxServerIntervalS = 24UL * 60UL * 60UL;

inline uint32_t normalizeServerInterval(const uint32_t intervalS) {
  if (intervalS < kMinServerIntervalS) {
    return kMinServerIntervalS;
  }
  if (intervalS > kMaxServerIntervalS) {
    return kMaxServerIntervalS;
  }
  return intervalS;
}

// Why a fetch did or did not start. Separated from the firmware so the guard *ordering*
// is testable: the three reasons are not interchangeable, and the difference between
// DeferNoHeap and a failed attempt is the whole point.
//
// A fetch needs a ~12 KB contiguous task stack. Right after boot the device does not have
// it, and that is normal — it has not finished settling. Treating that as a failed fetch
// would report last_fetch_ok = false and have_attempted = true (status lying about having
// contacted the server) and would arm the failure backoff for a server that was never asked.
enum class StartDecision : uint8_t {
  Start,
  AlreadyRunning,
  DeferBackoff,  // a real earlier failure is still being backed off
  DeferNoHeap,   // resource shortage, not a failure: record no attempt
};

inline StartDecision classifyStart(const bool fetchRunning, const bool backoffActive, const bool taskStackAvailable) {
  if (fetchRunning) {
    return StartDecision::AlreadyRunning;
  }
  if (backoffActive) {
    return StartDecision::DeferBackoff;
  }
  if (!taskStackAvailable) {
    return StartDecision::DeferNoHeap;
  }
  return StartDecision::Start;
}

inline uint32_t effectiveIntervalS(const bool lastAttemptSucceeded, const uint32_t serverIntervalS) {
  return lastAttemptSucceeded ? normalizeServerInterval(serverIntervalS) : kFailureRetryIntervalS;
}

inline bool refreshDue(const uint32_t nowEpoch, const uint32_t lastAttemptEpoch, const bool lastAttemptSucceeded,
                       const uint32_t serverIntervalS) {
  return wallclock::intervalElapsed(nowEpoch, lastAttemptEpoch,
                                    effectiveIntervalS(lastAttemptSucceeded, serverIntervalS));
}

// Pacing for devices whose wall clock is not usable (no NTP yet, or none ever).
//
// The epoch-based path above cannot help here: an unset clock means the "last attempt"
// timestamp can never be recorded, so a naive "due if we have no timestamp" test answers
// true forever and every background-server start refetches. Pace off the monotonic
// millisecond counter instead, which is always available.
//
// `nowMs` and `lastAttemptMs` are millis() values, so the comparison must be a signed
// difference to survive the ~49-day wrap.
inline bool refreshDueMonotonic(const uint32_t nowMs, const uint32_t lastAttemptMs, const bool haveAttempted,
                                const bool lastAttemptSucceeded, const uint32_t serverIntervalS) {
  if (!haveAttempted) {
    return true;  // never tried on this boot: fetch once immediately
  }
  const uint32_t intervalMs = effectiveIntervalS(lastAttemptSucceeded, serverIntervalS) * 1000UL;
  return static_cast<int32_t>(nowMs - lastAttemptMs) >= static_cast<int32_t>(intervalMs);
}

}  // namespace terminus_refresh
