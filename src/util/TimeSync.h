#pragma once

#include <FeatureFlags.h>

#include <ctime>

namespace TimeSync {
// Seeds system time from the last persisted NTP sync epoch.
// This makes time available early after reboot until WiFi can correct it.
void restorePersistedTime();

// Attempts to sync time with NTP using minimal memory and a short timeout.
// Returns true if time is (or becomes) valid.
bool syncTimeWithNtpLowMemory();

#if ENABLE_WIFI_CLOCK
// Starts a background NTP sync when WiFi is connected and the sync interval has elapsed.
// Safe to call from the main loop.
void loop(bool wifiConnected);

// Called when the fileserver web UI is actively used. It schedules a rate-limited
// immediate NTP refresh so the clock is corrected when a user is connected.
void noteWebUiAccess(bool wifiConnected);

// Sets the system clock to the given epoch and persists it.
// Used by the /api/time endpoint (manual time mode).
void setManualTime(std::time_t epoch);
#endif
}  // namespace TimeSync
