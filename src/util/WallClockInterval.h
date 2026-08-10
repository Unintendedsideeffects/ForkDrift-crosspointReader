#pragma once

#include <cstdint>

namespace wallclock {

// "Has at least minIntervalS elapsed since lastEpoch?"
//
// Wall-clock epoch seconds, NOT millis(): millis() resets across deep sleep, so
// any interval measured with it silently collapses to "always due" after every
// wake. LIBRARY_SHELF_MIN_INTERVAL_S already learned this the hard way.
//
// A lastEpoch in the FUTURE counts as elapsed. These are unsigned, so the
// subtraction would otherwise underflow to a value near UINT32_MAX and read as
// "not due" — leaving a periodic job wedged until real time caught up. That
// happens for real: a manual clock set or a bad persisted seed moves the clock
// backwards.
//
// lastEpoch == 0 means "never ran" and is always due.
inline bool intervalElapsed(const uint32_t nowEpoch, const uint32_t lastEpoch, const uint32_t minIntervalS) {
  if (lastEpoch == 0) {
    return true;
  }
  if (nowEpoch < lastEpoch) {
    return true;
  }
  return (nowEpoch - lastEpoch) >= minIntervalS;
}

// Epoch below which the system clock is assumed unset. Periodic jobs that gate
// on wall-clock time cannot measure an interval at all before this, so they must
// decide explicitly whether to run or skip rather than trusting the arithmetic.
// Same threshold BackgroundWifiService uses for the library shelf.
constexpr uint32_t kClockSetEpochThreshold = 1600000000UL;  // 2020-09-13

inline bool clockUsable(const uint32_t nowEpoch) { return nowEpoch > kClockSetEpochThreshold; }

}  // namespace wallclock
