#include "network/background/BackgroundWifiService.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <ctime>
#include <new>

#include "CrossPointState.h"
#include "HalStorage.h"
#include "OpdsServerStore.h"
#include "SpiBusMutex.h"
#include "core/features/FeatureLifecycle.h"
#include "core/registries/HeapReclaimRegistry.h"
#include "network/http/OpdsShelfFetcher.h"
#include "network/server/CrossPointWebServer.h"
#include "network/wifi/WifiScanCache.h"
#include "util/LibraryShelfStore.h"
#include "util/NetworkNames.h"
#include "util/WifiCredentialStore.h"

// Defined in CrossPointState.cpp — returns the FreeRTOS task that currently
// holds the pending-state mutex, or nullptr if unowned.
extern TaskHandle_t debugPendingStateMutexHolder();

BackgroundWifiService BackgroundWifiService::instance;

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR  // Host/simulator builds: ordinary static storage.
#endif

// Wall-clock stamp of the last successful library-shelf refresh. RTC memory so
// the interval gate spans deep sleep; zeroed on cold boot, which correctly
// forces a refresh after a reset or firmware update.
RTC_DATA_ATTR static time_t shelfLastRefreshEpoch = 0;

// Structure passed to the FreeRTOS task so it owns copies of the credentials
// and we don't hold pointers into the caller's stack after start() returns.
struct WifiTaskParams {
  char ssid[64];
  char password[64];
  bool useCurrentConnection = false;
};

void BackgroundWifiService::taskEntry(void* arg) {
  auto* params = static_cast<WifiTaskParams*>(arg);
  WifiTaskParams ownedParams = *params;
  delete params;
  instance.run(ownedParams.ssid, ownedParams.password, ownedParams.useCurrentConnection);
  vTaskDelete(nullptr);
}

bool BackgroundWifiService::startRetryActive() const {
  const unsigned long retryAt = nextStartAllowedMs;
  return retryAt != 0 && static_cast<int32_t>(millis() - retryAt) < 0;
}

void BackgroundWifiService::deferStartRetry(const char* reason) {
  nextStartAllowedMs = millis() + START_RETRY_MS;
  LOG_DBG("BGWIFI", "Background server start deferred (%s, heap: %u)", reason,
          static_cast<unsigned int>(ESP.getFreeHeap()));
  LOG_WRN("BGWIFI", "bg server backoff: waiting %lus", static_cast<unsigned long>(START_RETRY_MS / 1000));
}

bool BackgroundWifiService::canStartNow() {
  if (startRetryActive()) {
    return false;
  }
  const auto measure = [] {
    return background_server::StartResourceInput{
        .freeBytes = ESP.getFreeHeap(),
        .largestContiguousBytes = ESP.getMaxAllocHeap(),
        .taskStackBytes = TASK_STACK,
    };
  };

  background_server::StartResourceInput resources = measure();
  background_server::StartResourceVerdict verdict = background_server::evaluateStartResources(resources);

  // Home holds two caches — the composed cover framebuffer and the carousel
  // frame slot — that exist purely for redraw speed and rebuild from the SD
  // card. Together they are ~96 KB against a start requirement of ~41 KB, so
  // failing to start while holding them is a false shortage. Reclaim once and
  // re-measure rather than deferring for 30 s with the heap sitting in a cache.
  //
  // Deliberately after startRetryActive(): during backoff we return above and
  // never reach here, so an eviction can happen at most once per retry window
  // instead of on every reconcile tick.
  // Suppressed: cppcheck can't see HeapReclaimRegistry::add() callers (they are
  // in HomeActivity's TU), so it believes the registry is always empty here.
  // cppcheck-suppress knownConditionTrueFalse
  if (verdict != background_server::StartResourceVerdict::Ok && !core::HeapReclaimRegistry::empty()) {
    LOG_DBG("BGWIFI", "bg server start blocked (%u free, largest %u); reclaiming caches",
            static_cast<unsigned int>(resources.freeBytes),
            static_cast<unsigned int>(resources.largestContiguousBytes));
    core::HeapReclaimRegistry::releaseAll();
    resources = measure();
    verdict = background_server::evaluateStartResources(resources);
    LOG_DBG("BGWIFI", "bg server after reclaim: %u free, largest %u", static_cast<unsigned int>(resources.freeBytes),
            static_cast<unsigned int>(resources.largestContiguousBytes));
  }

  switch (verdict) {
    case background_server::StartResourceVerdict::Ok:
      return true;
    case background_server::StartResourceVerdict::InsufficientFree:
      LOG_WRN("BGWIFI", "bg server deferred: low heap (%u free, need %u)",
              static_cast<unsigned int>(resources.freeBytes),
              static_cast<unsigned int>(background_server::startMinFreeBytes(TASK_STACK)));
      deferStartRetry("low heap");
      return false;
    case background_server::StartResourceVerdict::InsufficientContiguous:
      // Distinct from low heap on purpose: total free can look ample while the
      // largest run is too small for the task stack. Logging them identically is
      // what made this failure mode invisible.
      LOG_WRN("BGWIFI", "bg server deferred: heap too fragmented (%u free, largest %u, need %u contiguous)",
              static_cast<unsigned int>(resources.freeBytes),
              static_cast<unsigned int>(resources.largestContiguousBytes), static_cast<unsigned int>(TASK_STACK));
      deferStartRetry("fragmented heap");
      return false;
  }
  return false;
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

    WIFI_STORE.setLastConnectedSsid(ssid);
    WIFI_STORE.saveToFile();
  }

  {
    const IPAddress ip = WiFi.localIP();
    LOG_DBG("BGWIFI", "Connected! IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connected = true;

    // Give network integrations a bounded pre-server window. CrossPointWebServer
    // route allocation fragments the remaining heap enough that Terminus cannot
    // subsequently allocate its proven-safe 12 KB download task stack.
    core::FeatureLifecycle::onBackgroundNetworkReady();
    // Unlike BackgroundWebServer (main loop), this runs on our own task, so blocking here
    // costs nothing but a delayed server start. Wait for any async work the hook started
    // so it gets the contiguous heap before the route-heavy server allocates.
    while (core::FeatureLifecycle::backgroundStartupDeferred()) {
      delay(50);
    }

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
    serving = true;
    LOG_DBG("BGWIFI", "Background web server running on port %d", server->getPort());

    char hostname[40];
    NetworkNames::getDeviceHostname(hostname, sizeof(hostname));
    if (MDNS.begin(hostname)) {
      mdnsStarted = true;
      LOG_DBG("BGWIFI", "mDNS started: http://%s.local/", hostname);
    } else {
      LOG_ERR("BGWIFI", "mDNS failed to start");
    }

    if (mdnsStarted && !shelfRefreshAttempted) {
      shelfRefreshAttempted = true;
      refreshLibraryShelf();
    }

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

    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
    }

    serving = false;
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
    // The radio is down, so any scan result the picker cached is stale.
    WifiScanCache::invalidate();
  }

  connected = false;
  serving = false;
  wifiOwned = false;
  keepWifiOnStop = false;

  // Publish completion only after the owner has torn down the server, mDNS,
  // radio state, and all upload/file resources.
  taskHandle = nullptr;
  serviceState = background_server::noteCleanupComplete(serviceState);
}

void BackgroundWifiService::refreshLibraryShelf() {
  const auto& opdsServers = OPDS_STORE.getServers();
  if (opdsServers.empty()) {
    LOG_DBG("BGWIFI", "Library shelf refresh skipped: no OPDS server");
    return;
  }

  // Interval gate. The service starts on every wake from sleep, and without
  // this the device performed an OPDS root fetch + parse + SD write each time.
  //
  // millis() is useless here — it restarts at 0 after every deep sleep wake,
  // which is precisely the interval we need to span. Wall-clock time does
  // survive (the RTC keeps running through deep sleep), and the last refresh
  // stamp lives in RTC memory so it survives with it. Before the clock is set
  // we cannot measure an interval at all, so we refresh rather than guess.
  const time_t nowEpoch = time(nullptr);
  const bool clockUsable = nowEpoch > CLOCK_SET_EPOCH_THRESHOLD;
  if (clockUsable && shelfLastRefreshEpoch > 0 && nowEpoch >= shelfLastRefreshEpoch &&
      (nowEpoch - shelfLastRefreshEpoch) < static_cast<time_t>(LIBRARY_SHELF_MIN_INTERVAL_S)) {
    LOG_DBG("BGWIFI", "Library shelf refresh skipped: refreshed %llds ago",
            static_cast<long long>(nowEpoch - shelfLastRefreshEpoch));
    return;
  }

  const uint32_t shelfHeapThreshold = LIBRARY_SHELF_MIN_HEAP_BYTES;
  if (ESP.getFreeHeap() < shelfHeapThreshold) {
    LOG_DBG("BGWIFI", "Library shelf refresh skipped: low heap (%u, need %u)",
            static_cast<unsigned int>(ESP.getFreeHeap()), static_cast<unsigned int>(shelfHeapThreshold));
    return;
  }

  std::vector<LibraryShelfEntry> shelfEntries;
  if (!OpdsShelfFetcher::fetchRootBooks(opdsServers[0], shelfEntries)) {
    LOG_DBG("BGWIFI", "Library shelf refresh failed");
    return;
  }

  {
    SpiBusMutex::Guard guard;
    LIBRARY_SHELF.replaceEntries(opdsServers[0].name, std::move(shelfEntries));
  }
  if (clockUsable) {
    shelfLastRefreshEpoch = nowEpoch;
  }
}

bool BackgroundWifiService::spawnTask(const char* ssid, const char* password, const bool useCurrentConnection,
                                      const char* logContext) {
  if (taskHandle != nullptr || !background_server::canStart(serviceState)) {
    LOG_ERR("BGWIFI", "Start rejected (%s): state=%u task=%p", logContext, static_cast<unsigned int>(serviceState),
            taskHandle);
    return false;
  }
  if (!canStartNow()) {
    return false;
  }

  stopRequested = false;
  keepWifiOnStop = false;
  connected = false;
  serving = false;
  wifiOwned = false;
  requestCount = 0;
  mdnsStarted = false;
  shelfRefreshAttempted = false;
  serviceState = background_server::ServiceState::Running;

  // Heap-allocate params so the pointers remain valid after this function returns
  auto* params = new (std::nothrow) WifiTaskParams();
  if (params == nullptr) {
    LOG_ERR("BGWIFI", "Failed to allocate WiFi task params");
    serviceState = background_server::ServiceState::Stopped;
    deferStartRetry("params alloc failed");
    return false;
  }

  strncpy(params->ssid, ssid ? ssid : "", sizeof(params->ssid) - 1);
  params->ssid[sizeof(params->ssid) - 1] = '\0';
  strncpy(params->password, password ? password : "", sizeof(params->password) - 1);
  params->password[sizeof(params->password) - 1] = '\0';
  params->useCurrentConnection = useCurrentConnection;

  const BaseType_t result =
      xTaskCreate(&BackgroundWifiService::taskEntry, "bgwifi", TASK_STACK, params, 1, &taskHandle);

  if (result != pdPASS) {
    LOG_ERR("BGWIFI", "Failed to create task (heap: %d bytes free)", ESP.getFreeHeap());
    delete params;
    taskHandle = nullptr;
    serviceState = background_server::ServiceState::Stopped;
    deferStartRetry("task create failed");
    return false;
  }

  LOG_DBG("BGWIFI", "Background WiFi task started (%s)", logContext);
  return true;
}

bool BackgroundWifiService::start(const char* ssid, const char* password) {
  return spawnTask(ssid, password, false, "start");
}

bool BackgroundWifiService::startUsingCurrentConnection() {
  return spawnTask(nullptr, nullptr, true, "adopt current connection");
}

bool BackgroundWifiService::stop(const bool keepWifi) {
  if (taskHandle == nullptr) {
    return background_server::canStart(serviceState);
  }

  LOG_DBG("BGWIFI", "Requesting stop...");
  keepWifiOnStop = keepWifi;
  stopRequested = true;
  serviceState = background_server::requestStop(serviceState);

  // Wait for the task to exit. The bg loop checks stopRequested between
  // handleClient() iterations, so the only way to exceed this is a single
  // request handler blocking for >STOP_TIMEOUT_MS (e.g. a large upload).
  // 30 s is enough for normal upload completion; force-delete below is a
  // last resort that orphans pendingStateMutex and bricks subsequent Gives.
  constexpr unsigned long STOP_TIMEOUT_MS = 30000;
  const unsigned long stopBegunMs = millis();
  const unsigned long deadline = stopBegunMs + STOP_TIMEOUT_MS;
  // The service loop notices stopRequested within a tick, so the overwhelming
  // majority of stops complete in a few ms. Poll at 1 ms for the first 100 ms
  // — a flat 10 ms poll turned a ~2 ms teardown into a 10 ms stall on every
  // activity entry that releases the radio — then back off while waiting out
  // the rare in-flight upload.
  while (taskHandle != nullptr && millis() < deadline) {
    delay((millis() - stopBegunMs) < 100 ? 1 : 10);
  }

  if (taskHandle != nullptr) {
    const bool heldPendingMutex = (debugPendingStateMutexHolder() == taskHandle);
    const bool heldStorageMutex = (HalStorage::storageMutexHolder() == taskHandle);
    serviceState = background_server::noteStopTimeout(serviceState);
    LOG_ERR("BGWIFI",
            "Cooperative stop timed out after %lu ms: state=wedged requests=%lu serving=%d heap=%u "
            "pending_mutex=%d storage_mutex=%d; restart disabled until owner cleanup or reboot",
            STOP_TIMEOUT_MS, requestCount, serving ? 1 : 0, static_cast<unsigned int>(ESP.getFreeHeap()),
            heldPendingMutex ? 1 : 0, heldStorageMutex ? 1 : 0);
    return false;
  }

  LOG_DBG("BGWIFI", "Stopped. Total requests served: %lu", requestCount);
  return true;
}
