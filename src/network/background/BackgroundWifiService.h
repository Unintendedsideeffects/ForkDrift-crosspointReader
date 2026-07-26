#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

#include "network/background/BackgroundServerPolicy.h"

/**
 * BackgroundWifiService manages a silent WiFi connection and web server
 * that runs alongside the normal activity stack. Used for WiFi auto-connect
 * on wake from sleep so users can push files or sync progress without
 * manually opening File Transfer.
 *
 * Lifecycle:
 *   start(ssid, password) — spawns a FreeRTOS task that connects + serves
 *   stop()                — requests owner-task teardown and waits for acknowledgement
 *   isRunning()           — true while task is active
 *   getRequestCount()     — HTTP requests handled since start()
 */
class CrossPointWebServer;

class BackgroundWifiService {
  static BackgroundWifiService instance;

  CrossPointWebServer* server = nullptr;
  TaskHandle_t taskHandle = nullptr;
  volatile bool stopRequested = false;
  volatile bool keepWifiOnStop = false;
  volatile bool connected = false;
  volatile bool serving = false;
  volatile bool wifiOwned = false;
  volatile uint32_t requestCount = 0;
  volatile unsigned long nextStartAllowedMs = 0;
  volatile bool mdnsStarted = false;
  volatile bool shelfRefreshAttempted = false;
  volatile background_server::ServiceState serviceState = background_server::ServiceState::Stopped;

  // FreeRTOS task entry point
  static void taskEntry(void* arg);
  void run(const char* ssid, const char* password, bool useCurrentConnection);
  // Shared by start() and startUsingCurrentConnection(): resets the volatile
  // state, allocates the task params and spawns the task.
  bool spawnTask(const char* ssid, const char* password, bool useCurrentConnection, const char* logContext);
  void refreshLibraryShelf();
  bool canStartNow();
  bool startRetryActive() const;
  void deferStartRetry(const char* reason);

  // Route-heavy CrossPointWebServer handlers run on this task in Always mode.
  static constexpr uint32_t TASK_STACK = 8192;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t START_RETRY_MS = 30000;
  static constexpr uint32_t MIN_START_HEAP_BYTES = 60000;
  static constexpr uint32_t LIBRARY_SHELF_HEAP_MARGIN_BYTES = 24000;
  // The shelf refresh is an HTTP fetch plus an SD write. Without an interval
  // gate it ran on every single background start — i.e. every wake from sleep.
  // Measured in wall-clock seconds because millis() resets across deep sleep.
  static constexpr uint32_t LIBRARY_SHELF_MIN_INTERVAL_S = 15UL * 60UL;
  // Epoch below which the system clock is assumed unset (2020-09-13).
  static constexpr long CLOCK_SET_EPOCH_THRESHOLD = 1600000000L;

 public:
  BackgroundWifiService() = default;
  BackgroundWifiService(const BackgroundWifiService&) = delete;
  BackgroundWifiService& operator=(const BackgroundWifiService&) = delete;

  static BackgroundWifiService& getInstance() { return instance; }

  // Start background WiFi + web server (no-op if already running).
  // Returns true when a background task was created.
  bool start(const char* ssid, const char* password);

  // Start the background web server using the current STA connection.
  bool startUsingCurrentConnection();

  // Stop the background task cooperatively. Returns false if the owning task
  // did not acknowledge cleanup before the timeout; a wedged service rejects
  // restart until that task eventually completes or the device reboots.
  bool stop(bool keepWifi = false);

  bool isRunning() const { return taskHandle != nullptr; }
  // Returns true while the task is active OR while a deferred start retry is pending.
  // Use this in reconcile loops to suppress repeated start attempts during the retry window.
  bool isPendingOrRunning() const { return taskHandle != nullptr || startRetryActive(); }
  bool isConnected() const { return connected; }
  bool isServing() const { return serving; }
  background_server::ServiceState getServiceState() const { return serviceState; }
  bool isWedged() const { return serviceState == background_server::ServiceState::Wedged; }
  uint32_t getRequestCount() const { return requestCount; }
  bool hadApiActivity() const { return requestCount > 0; }
};

#define BG_WIFI BackgroundWifiService::getInstance()
