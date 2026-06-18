#pragma once

#include <FeatureFlags.h>

#include <ctime>

namespace TimeSync {
// Seeds system time from the last persisted NTP sync epoch.
// This makes time available early after reboot until WiFi can correct it.
void restorePersistedTime();

// Attempts to sync time with NTP using minimal memory and a short timeout.
// Returns true if time is (or becomes) valid.
// When `force` is true, bypasses the throttling gate and always attempts a sync
// (used by manual user-initiated sync flows).
bool syncTimeWithNtpLowMemory(bool force = false);

// Blocks on an NTP sync when the clock has not been confirmed this session, so
// callers that verify TLS server certificates (OTA, KOReader, TRMNL) reject
// stale-clock false negatives. Returns true once the wall clock is trustworthy.
// No-op cost once the clock is confirmed; safe to call before every secure
// request. Requires an active WiFi connection to actually sync.
bool ensureTrustedClock();

#if ENABLE_WIFI_CLOCK
// Starts a background NTP sync when WiFi is connected and the sync interval has elapsed.
// Safe to call from the main loop.
void loop(bool wifiConnected);

// Called when the fileserver web UI is actively used. It schedules a rate-limited
// immediate NTP refresh so the clock is corrected when a user is connected.
void noteWebUiAccess(bool wifiConnected);

// Called when an always-on/on-charge background server is actively contacted.
// Opt-in via settings; returns immediately and only schedules the existing
// background NTP path.
void noteBackgroundServerAccess(bool wifiConnected);

// Sets the system clock to the given epoch and persists it.
// Used by the /api/time endpoint (manual time mode).
void setManualTime(std::time_t epoch);
#endif
}  // namespace TimeSync
