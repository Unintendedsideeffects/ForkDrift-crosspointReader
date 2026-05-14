#include "BackgroundWifiService.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <new>

#include "network/CrossPointWebServer.h"

BackgroundWifiService BackgroundWifiService::instance;

// Structure passed to the FreeRTOS task so it owns copies of the credentials
// and we don't hold pointers into the caller's stack after start() returns.
struct WifiTaskParams {
  char ssid[64];
  char password[64];
  bool useCurrentConnection = false;
};

void BackgroundWifiService::taskEntry(void* arg) {
  auto* params = static_cast<WifiTaskParams*>(arg);
  instance.run(params->ssid, params->password, params->useCurrentConnection);
  delete params;
}

bool BackgroundWifiService::startRetryActive() const {
  const unsigned long retryAt = nextStartAllowedMs;
  return retryAt != 0 && static_cast<int32_t>(millis() - retryAt) < 0;
}

void BackgroundWifiService::deferStartRetry(const char* reason) {
  nextStartAllowedMs = millis() + START_RETRY_MS;
  LOG_DBG("BGWIFI", "Background server start deferred (%s, heap: %u)", reason,
          static_cast<unsigned int>(ESP.getFreeHeap()));
}

bool BackgroundWifiService::canStartNow() {
  if (startRetryActive()) {
    return false;
  }
  if (ESP.getFreeHeap() < MIN_START_HEAP_BYTES) {
    deferStartRetry("low heap");
    return false;
  }
  return true;
}

void BackgroundWifiService::run(const char* ssid, const char* password, const bool useCurrentConnection) {
  wifiOwned = false;
  bool serverStartFailed = false;

  if (useCurrentConnection) {
    LOG_DBG("BGWIFI", "Starting background web server on existing WiFi connection");
    if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      LOG_DBG("BGWIFI", "Existing WiFi connection unavailable");
      goto cleanup;
    }
  } else {
    LOG_DBG("BGWIFI", "Starting background WiFi, SSID: %s", ssid);

    // ── Connect ────────────────────────────────────────────────────────────
    wifiOwned = true;
    WiFi.mode(WIFI_STA);
    if (password && password[0] != '\0') {
      WiFi.begin(ssid, password);
    } else {
      WiFi.begin(ssid);
    }

    const unsigned long connectDeadline = millis() + CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < connectDeadline) {
      if (stopRequested) {
        LOG_DBG("BGWIFI", "Stop requested during connect");
        goto cleanup;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      LOG_DBG("BGWIFI", "Connection timed out");
      goto cleanup;
    }
  }

  {
    const IPAddress ip = WiFi.localIP();
    LOG_DBG("BGWIFI", "Connected! IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connected = true;

    // ── Start web server ──────────────────────────────────────────────────
    server = new (std::nothrow) CrossPointWebServer();
    if (server == nullptr) {
      LOG_ERR("BGWIFI", "Failed to allocate CrossPointWebServer");
      serverStartFailed = true;
      goto cleanup;
    }

    server->begin();

    if (!server->isRunning()) {
      LOG_ERR("BGWIFI", "Web server failed to start");
      delete server;
      server = nullptr;
      serverStartFailed = true;
      goto cleanup;
    }

    nextStartAllowedMs = 0;
    LOG_DBG("BGWIFI", "Background web server running on port %d", server->getPort());

    // ── Service loop ──────────────────────────────────────────────────────
    while (!stopRequested) {
      esp_task_wdt_reset();

      if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
        LOG_DBG("BGWIFI", "WiFi disconnected; stopping background server");
        break;
      }

      server->handleClient();
      requestCount = server->getRequestCount();  // Propagate to volatile field
      vTaskDelay(pdMS_TO_TICKS(1));              // Yield to scheduler
    }

    LOG_DBG("BGWIFI", "Background task stopping. Requests served: %lu", requestCount);

    server->stop();
    delete server;
    server = nullptr;
  }

cleanup:
  if (serverStartFailed && !stopRequested) {
    deferStartRetry("server start failed");
  }

  if (wifiOwned && !(stopRequested && keepWifiOnStop)) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
    delay(30);
  }

  connected = false;
  wifiOwned = false;
  keepWifiOnStop = false;

  // Signal stop() that we've exited, then self-delete
  taskHandle = nullptr;
  vTaskDelete(nullptr);
}

void BackgroundWifiService::start(const char* ssid, const char* password) {
  if (taskHandle != nullptr) {
    LOG_DBG("BGWIFI", "Already running, ignoring start()");
    return;
  }
  if (!canStartNow()) {
    return;
  }

  stopRequested = false;
  keepWifiOnStop = false;
  connected = false;
  wifiOwned = false;
  requestCount = 0;

  // Heap-allocate params so the pointers remain valid after this function returns
  auto* params = new (std::nothrow) WifiTaskParams();
  if (params == nullptr) {
    LOG_ERR("BGWIFI", "Failed to allocate WiFi task params");
    deferStartRetry("params alloc failed");
    return;
  }

  strncpy(params->ssid, ssid, sizeof(params->ssid) - 1);
  params->ssid[sizeof(params->ssid) - 1] = '\0';
  strncpy(params->password, password ? password : "", sizeof(params->password) - 1);
  params->password[sizeof(params->password) - 1] = '\0';
  params->useCurrentConnection = false;

  const BaseType_t result =
      xTaskCreate(&BackgroundWifiService::taskEntry, "bgwifi", TASK_STACK, params, 1, &taskHandle);

  if (result != pdPASS) {
    LOG_ERR("BGWIFI", "Failed to create task (heap: %d bytes free)", ESP.getFreeHeap());
    delete params;
    taskHandle = nullptr;
    deferStartRetry("task create failed");
  } else {
    LOG_DBG("BGWIFI", "Background WiFi task started");
  }
}

void BackgroundWifiService::startUsingCurrentConnection() {
  if (taskHandle != nullptr) {
    LOG_DBG("BGWIFI", "Already running, ignoring startUsingCurrentConnection()");
    return;
  }
  if (!canStartNow()) {
    return;
  }

  stopRequested = false;
  keepWifiOnStop = false;
  connected = false;
  wifiOwned = false;
  requestCount = 0;

  auto* params = new (std::nothrow) WifiTaskParams();
  if (params == nullptr) {
    LOG_ERR("BGWIFI", "Failed to allocate WiFi task params");
    deferStartRetry("params alloc failed");
    return;
  }

  params->ssid[0] = '\0';
  params->password[0] = '\0';
  params->useCurrentConnection = true;

  const BaseType_t result =
      xTaskCreate(&BackgroundWifiService::taskEntry, "bgwifi", TASK_STACK, params, 1, &taskHandle);

  if (result != pdPASS) {
    LOG_ERR("BGWIFI", "Failed to create task (heap: %d bytes free)", ESP.getFreeHeap());
    delete params;
    taskHandle = nullptr;
    deferStartRetry("task create failed");
  } else {
    LOG_DBG("BGWIFI", "Background WiFi task started on existing connection");
  }
}

void BackgroundWifiService::stop(const bool keepWifi) {
  if (taskHandle == nullptr) {
    return;
  }

  LOG_DBG("BGWIFI", "Requesting stop...");
  keepWifiOnStop = keepWifi;
  stopRequested = true;

  // Wait for the task to exit. The bg loop checks stopRequested between
  // handleClient() iterations, so the only way to exceed this is a single
  // request handler blocking for >STOP_TIMEOUT_MS (e.g. a large upload).
  // 30 s is enough for normal upload completion; force-delete below is a
  // last resort that orphans pendingStateMutex and bricks subsequent Gives.
  constexpr unsigned long STOP_TIMEOUT_MS = 30000;
  const unsigned long deadline = millis() + STOP_TIMEOUT_MS;
  while (taskHandle != nullptr && millis() < deadline) {
    delay(10);
  }

  if (taskHandle != nullptr) {
    // Task didn't exit cleanly — force-delete as last resort.
    // WARNING: force-killing a task that holds a non-recursive mutex leaves
    // FreeRTOS believing the now-dead task still owns it. The next Give from
    // any other task then trips xQueueGenericSend assert at queue.c:832.
    extern TaskHandle_t debugPendingStateMutexHolder();
    const bool heldMutex = (debugPendingStateMutexHolder() == taskHandle);
    LOG_ERR("BGWIFI", "Task did not exit within %lu ms, force-deleting (held pendingStateMutex=%d)", STOP_TIMEOUT_MS,
            heldMutex ? 1 : 0);
    vTaskDelete(taskHandle);
    taskHandle = nullptr;
    connected = false;
    if (wifiOwned && !keepWifi) {
      WiFi.disconnect(false);
      WiFi.mode(WIFI_OFF);
    }
    wifiOwned = false;
    keepWifiOnStop = false;
  }

  LOG_DBG("BGWIFI", "Stopped. Total requests served: %lu", requestCount);
}
