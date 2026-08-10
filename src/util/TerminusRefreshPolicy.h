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

inline bool refreshDue(const uint32_t nowEpoch, const uint32_t lastAttemptEpoch, const bool lastAttemptSucceeded,
                       const uint32_t serverIntervalS) {
  const uint32_t intervalS =
      lastAttemptSucceeded ? normalizeServerInterval(serverIntervalS) : kFailureRetryIntervalS;
  return wallclock::intervalElapsed(nowEpoch, lastAttemptEpoch, intervalS);
}

}  // namespace terminus_refresh
