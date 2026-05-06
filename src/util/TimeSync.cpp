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
#if ENABLE_WIFI_CLOCK
constexpr unsigned long kSyncAttemptIntervalMs = 15UL * 60UL * 1000UL;
volatile bool syncTaskRunning = false;
volatile bool syncRequested = false;
#endif

bool isTimeValid() {
  const std::time_t now = std::time(nullptr);
  return now >= kMinValidTime;
}

bool shouldSync() {
  const auto mode = static_cast<CrossPointSettings::TIME_MODE>(SETTINGS.timeMode);
  if (mode == CrossPointSettings::TIME_MODE_MANUAL) {
    return false;
  }
  if (!isTimeValid()) {
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
  TimeSync::syncTimeWithNtpLowMemory();
  syncTaskRunning = false;
  vTaskDelete(nullptr);
}
#endif
}  // namespace

namespace TimeSync {
void restorePersistedTime() {
  if (isTimeValid() || SETTINGS.lastTimeSyncEpoch < kMinValidTime) {
    return;
  }

  const std::time_t restored = static_cast<std::time_t>(SETTINGS.lastTimeSyncEpoch) + (millis() / 1000);
  if (restored < kMinValidTime) {
    return;
  }

  timeval tv{};
  tv.tv_sec = restored;
  settimeofday(&tv, nullptr);
  LOG_DBG("TIMESYNC", "Restored persisted time seed: %lu", static_cast<unsigned long>(restored));
}

bool syncTimeWithNtpLowMemory() {
  if (!shouldSync()) {
    return isTimeValid();
  }

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  constexpr int maxRetries = 30;  // ~3s
  for (int retry = 0; retry < maxRetries; retry++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      const std::time_t now = std::time(nullptr);
      if (now >= kMinValidTime) {
        SETTINGS.lastTimeSyncEpoch = static_cast<uint32_t>(now);
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
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("TIMESYNC", "Failed to persist time sync epoch to SD card");
    }
    return true;
  }
  return false;
}

#if ENABLE_WIFI_CLOCK
void loop(const bool wifiConnected) {
  static unsigned long lastAttemptMs = 0;

  if (!wifiConnected || syncTaskRunning) {
    return;
  }

  const unsigned long nowMs = millis();
  const bool requested = syncRequested;
  if (!requested && lastAttemptMs != 0 && nowMs - lastAttemptMs < kSyncAttemptIntervalMs) {
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

void noteWebUiAccess(const bool wifiConnected) {
  static unsigned long lastWebAccessSyncMs = 0;

  if (!wifiConnected || syncTaskRunning) {
    return;
  }

  const unsigned long nowMs = millis();
  if (lastWebAccessSyncMs != 0 && nowMs - lastWebAccessSyncMs < kSyncAttemptIntervalMs) {
    return;
  }

  lastWebAccessSyncMs = nowMs;
  syncRequested = true;
}
#endif
}  // namespace TimeSync
