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
