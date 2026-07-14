#include "TimeSync.h"

#include <Arduino.h>
#include <FeatureFlags.h>
#include <Logging.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>

#include <ctime>

#include "CrossPointSettings.h"

namespace {
constexpr std::time_t kMinValidTime = 1577836800;  // 2020-01-01 00:00:00 UTC

// Persisted time loaded at boot is only a stale snapshot of "last successful
// sync". Until NTP corrects it this session we treat it as untrusted so
// shouldSync() forces an attempt regardless of the 23-hour throttle.
// volatile: written by the background NTP task, read by the main task in
// shouldSync(); without it the compiler may cache the read and never observe
// the clear, forcing endless re-sync attempts.
volatile bool gPersistedTimeUntrusted = false;

#if ENABLE_WIFI_CLOCK
// Steady-state interval after a successful sync.
constexpr unsigned long kSyncSteadyIntervalMs = 15UL * 60UL * 1000UL;

// Backoff ladder after a failed sync: 30s, 1m, 2m, 5m. After that, fall back
// to the steady 15m interval.
constexpr unsigned long kBackoffLadderMs[] = {
    30UL * 1000UL,
    60UL * 1000UL,
    2UL * 60UL * 1000UL,
    5UL * 60UL * 1000UL,
};
constexpr size_t kBackoffLadderLen = sizeof(kBackoffLadderMs) / sizeof(kBackoffLadderMs[0]);

volatile bool syncTaskRunning = false;
volatile bool syncRequested = false;
// 0 = no result pending, 1 = last task succeeded, -1 = last task failed.
volatile int8_t pendingResult = 0;
uint8_t consecutiveFailures = 0;
bool lastWifiConnected = false;

unsigned long nextAttemptDelayMs() {
  if (consecutiveFailures == 0) {
    return kSyncSteadyIntervalMs;
  }
  const size_t idx =
      (consecutiveFailures - 1u < kBackoffLadderLen) ? (consecutiveFailures - 1u) : (kBackoffLadderLen - 1u);
  return kBackoffLadderMs[idx];
}
#endif

bool isTimeValid() {
  const std::time_t now = std::time(nullptr);
  return now >= kMinValidTime;
}

bool shouldSync(bool force = false) {
  if (force) {
    return true;
  }
  const auto mode = static_cast<CrossPointSettings::TIME_MODE>(SETTINGS.timeMode);
  if (mode == CrossPointSettings::TIME_MODE_MANUAL) {
    return false;
  }
  if (!isTimeValid()) {
    return true;
  }
  // Persisted-time seed hasn't been confirmed by NTP yet this session.
  if (gPersistedTimeUntrusted) {
    return true;
  }
  if (SETTINGS.lastTimeSyncEpoch == 0) {
    return true;
  }
  const std::time_t now = std::time(nullptr);
  const uint32_t lastSync = SETTINGS.lastTimeSyncEpoch;
  constexpr std::time_t minInterval = 23 * 60 * 60;
  if (now >= static_cast<std::time_t>(lastSync) && now - lastSync < minInterval) {
    return false;
  }
  return true;
}

#if ENABLE_WIFI_CLOCK
void backgroundSyncTask(void* /*param*/) {
  // Background sync always uses the throttled path; manual force=true syncs
  // come from ClockSyncActivity which calls syncTimeWithNtpLowMemory directly.
  const bool ok = TimeSync::syncTimeWithNtpLowMemory(/*force=*/false);
  pendingResult = ok ? 1 : -1;
  syncTaskRunning = false;
  vTaskDelete(nullptr);
}
#endif
}  // namespace

namespace TimeSync {
void restorePersistedTime() {
  if (isTimeValid()) {
    // System time survived (e.g. deep-sleep wake) — leave it alone.
    return;
  }
  if (SETTINGS.lastTimeSyncEpoch < kMinValidTime) {
    return;
  }

  // Seed wall-clock from the last known good epoch. We deliberately do NOT
  // add millis()/1000 — that only accounts for this boot's uptime, not the
  // real (unknown) off-time. NTP will correct this once WiFi is up.
  timeval tv{};
  tv.tv_sec = static_cast<std::time_t>(SETTINGS.lastTimeSyncEpoch);
  settimeofday(&tv, nullptr);
  gPersistedTimeUntrusted = true;
  LOG_DBG("TIMESYNC", "Restored persisted time seed: %lu (untrusted until NTP)",
          static_cast<unsigned long>(SETTINGS.lastTimeSyncEpoch));
}

bool syncTimeWithNtpLowMemory(const bool force) {
  if (!shouldSync(force)) {
    return isTimeValid();
  }

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_setservername(2, "time.cloudflare.com");
  esp_sntp_init();

  const std::time_t epochBefore = std::time(nullptr);

  // 10s budget — enough for DNS + 2-3 SNTP round trips on a slow network.
  constexpr int maxRetries = 100;
  for (int retry = 0; retry < maxRetries; retry++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      const std::time_t now = std::time(nullptr);
      if (now >= kMinValidTime) {
        const long delta = (epochBefore >= kMinValidTime) ? static_cast<long>(now - epochBefore) : 0;
        LOG_INF("TIMESYNC", "NTP sync ok: epoch=%lu (delta=%lds)", static_cast<unsigned long>(now), delta);
        SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(now);
        gPersistedTimeUntrusted = false;
        if (!SETTINGS.saveToFile()) {
          LOG_WRN("TIMESYNC", "Failed to persist time sync epoch to SD card");
        }
      }
      return isTimeValid();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  if (isTimeValid()) {
    const std::time_t now = std::time(nullptr);
    SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(now);
    gPersistedTimeUntrusted = false;
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("TIMESYNC", "Failed to persist time sync epoch to SD card");
    }
    return true;
  }
  LOG_WRN("TIMESYNC", "NTP sync timed out after %d retries", maxRetries);
  return false;
}

bool ensureTrustedClock() {
  if (isTimeValid() && !gPersistedTimeUntrusted) {
    return true;
  }
  return syncTimeWithNtpLowMemory(/*force=*/true);
}

#if ENABLE_WIFI_CLOCK
void loop(const bool wifiConnected) {
  static unsigned long lastAttemptMs = 0;

  // Reconcile any completed background task's result into the backoff counter.
  if (pendingResult != 0 && !syncTaskRunning) {
    if (pendingResult == 1) {
      consecutiveFailures = 0;
    } else if (consecutiveFailures < 0xFF) {
      consecutiveFailures++;
    }
    pendingResult = 0;
  }

  // WiFi rising edge — force an immediate attempt so we don't wait the full
  // 15-minute steady interval after fresh connection.
  const bool wifiJustConnected = wifiConnected && !lastWifiConnected;
  lastWifiConnected = wifiConnected;

  if (!wifiConnected || syncTaskRunning) {
    return;
  }

  const unsigned long nowMs = millis();
  const bool requested = syncRequested || wifiJustConnected;
  const unsigned long requiredGap = nextAttemptDelayMs();
  if (!requested && lastAttemptMs != 0 && nowMs - lastAttemptMs < requiredGap) {
    return;
  }

  if (!requested && !shouldSync()) {
    lastAttemptMs = nowMs;
    return;
  }

  syncTaskRunning = true;
  syncRequested = false;
  lastAttemptMs = nowMs;
  if (xTaskCreate(backgroundSyncTask, "TimeSyncTask", 4096, nullptr, 1, nullptr) != pdPASS) {
    syncTaskRunning = false;
    LOG_ERR("TIMESYNC", "Failed to start time sync task");
  }
}

void noteWebUiAccess(const bool wifiConnected) { noteBackgroundServerAccess(wifiConnected); }

void noteBackgroundServerAccess(const bool wifiConnected) {
  static unsigned long lastWebAccessSyncMs = 0;

  if (!SETTINGS.autoSyncDayOnBackgroundPing) {
    return;
  }

  if (!wifiConnected || syncTaskRunning) {
    return;
  }

  const unsigned long nowMs = millis();
  if (lastWebAccessSyncMs != 0 && nowMs - lastWebAccessSyncMs < kSyncSteadyIntervalMs) {
    return;
  }

  lastWebAccessSyncMs = nowMs;
  syncRequested = true;
}

void setManualTime(const std::time_t epoch) {
  timeval tv{};
  tv.tv_sec = epoch;
  settimeofday(&tv, nullptr);
  SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(epoch);
  gPersistedTimeUntrusted = false;
  consecutiveFailures = 0;
  if (!SETTINGS.saveToFile()) {
    LOG_WRN("TIMESYNC", "Failed to persist manual time");
  }
  LOG_INF("TIMESYNC", "Manual time set: %lu", static_cast<unsigned long>(epoch));
}
#endif
}  // namespace TimeSync
