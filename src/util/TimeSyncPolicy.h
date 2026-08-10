#pragma once

#include <cstdint>

#include "util/WallClockInterval.h"

namespace timesync {

// The clock resync floor.
//
// TimeSync::loop() polls every kSyncSteadyIntervalMs (15 min), but this gate
// decides whether a poll actually syncs. It used to be 23 hours, so the real
// cadence was once per day and the clock drifted visibly in between — the 15
// minute figure was only the polling rate, never the sync cadence.
//
// Each successful sync rewrites the whole settings file (TimeSync.cpp calls
// SETTINGS.saveToFile() on every success), so the cadence has a direct write
// cost: a 15 minute floor is ~96 whole-file writes per day, one hour is ~24.
// One hour keeps drift bounded while staying 23x more frequent than the old
// floor. Tune here, not at the call site.
constexpr uint32_t kMinResyncIntervalS = 60UL * 60UL;

// True when a resync is due. Delegates to the shared wall-clock predicate so the
// backwards-clock and never-run cases have exactly one implementation across the
// clock, Terminus and KOReader heartbeats.
inline bool resyncDue(const uint32_t nowEpoch, const uint32_t lastSyncEpoch,
                      const uint32_t minIntervalS = kMinResyncIntervalS) {
  return wallclock::intervalElapsed(nowEpoch, lastSyncEpoch, minIntervalS);
}

}  // namespace timesync
