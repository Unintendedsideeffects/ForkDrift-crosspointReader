#include "network/background/BackgroundWifiCoordinator.h"

#include <Arduino.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <WiFi.h>

#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "core/features/FeatureModules.h"
#include "network/background/BackgroundServerPolicy.h"
#include "network/background/BackgroundWifiService.h"
#include "network/wifi/WifiScanCache.h"
#include "network/wifi/WifiUtil.h"
#include "util/WifiCredentialStore.h"

BackgroundWifiCoordinator BackgroundWifiCoordinator::instance;

background_server::AutoConnectInput BackgroundWifiCoordinator::buildAutoConnectInput() const {
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const std::optional<WifiCredential> cred = WIFI_STORE.findCredential(lastSsid);

  return background_server::AutoConnectInput{
      .connectWanted = sessionRequested_,
      .waitingForNewCredential = APP_STATE.wifiAutoConnectWaitingForNewCredential,
      .lastConnectedSsid = lastSsid,
      .hasCredentialForLastSsid = cred.has_value(),
  };
}

bool BackgroundWifiCoordinator::attemptAutoConnect(const char* logTag) {
  const background_server::AutoConnectDecision decision =
      background_server::evaluateAutoConnect(buildAutoConnectInput());

  // reconcile() runs every tick; only log a skip reason when it changes.
  const int actionCode = static_cast<int>(decision.action);
  const bool reasonChanged = actionCode != lastAutoConnectLoggedAction_;
  lastAutoConnectLoggedAction_ = actionCode;

  switch (decision.action) {
    case background_server::AutoConnectAction::None:
      return false;
    case background_server::AutoConnectAction::NoLastSsid:
      if (reasonChanged) {
        LOG_DBG(logTag, "WiFi auto-connect has no last-connected SSID to target");
      }
      return false;
    case background_server::AutoConnectAction::BlockedWaitingForCredential:
      // WRN, not DBG: from the user's side the device simply never joins WiFi,
      // and every network feature is dark until they notice. A silent skip here
      // is what made this cost a full on-device debugging session. Still gated
      // on a change of reason — this runs every tick, and a per-tick WRN is its
      // own kind of invisible.
      if (reasonChanged) {
        LOG_WRN(logTag, "WiFi auto-connect disabled until a new credential is added");
      }
      return false;
    case background_server::AutoConnectAction::ClearStaleCredentialLatchAndStart: {
      const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
      const std::optional<WifiCredential> cred = WIFI_STORE.findCredential(lastSsid);
      if (!cred) {  // Raced with a forget between decision and use.
        return false;
      }
      APP_STATE.wifiAutoConnectWaitingForNewCredential = false;
      if (!APP_STATE.saveToFile()) {
        LOG_WRN(logTag, "Failed to persist cleared WiFi credential recovery state");
      }
      LOG_INF(logTag, "Credential for %s is present again; clearing stale auto-connect block", lastSsid.c_str());
      return BG_WIFI.start(cred->ssid.c_str(), cred->password.c_str());
    }
    case background_server::AutoConnectAction::MissingCredentialForLastSsid: {
      APP_STATE.wifiAutoConnectWaitingForNewCredential = true;
      if (!APP_STATE.saveToFile()) {
        LOG_WRN(logTag, "Failed to persist WiFi credential recovery state");
      }
      LOG_DBG(logTag,
              "Saved WiFi credentials missing for last SSID; auto-connect disabled until a new credential is added");
      return false;
    }
    case background_server::AutoConnectAction::StartWithLastCredential: {
      const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
      const std::optional<WifiCredential> cred = WIFI_STORE.findCredential(lastSsid);
      if (!cred) {
        return false;
      }
      LOG_DBG(logTag, "Starting background WiFi auto-connect to: %s", lastSsid.c_str());
      return BG_WIFI.start(cred->ssid.c_str(), cred->password.c_str());
    }
  }

  return false;
}

void BackgroundWifiCoordinator::applyReconcileDecision(const background_server::ReconcileDecision& decision) {
  switch (decision.action) {
    case background_server::ReconcileAction::None:
      return;
    case background_server::ReconcileAction::StopBgWifi:
      BG_WIFI.stop(decision.stopKeepWifi);
      return;
    case background_server::ReconcileAction::StartUsingCurrentConnection:
      BG_WIFI.startUsingCurrentConnection();
      return;
    case background_server::ReconcileAction::AttemptAutoConnect:
      if (attemptAutoConnect("BGCOORD")) {
        wifiAutoConnectAttempted_ = true;
      } else {
        // No credential / blocked latch: a session that cannot connect ends here
        // instead of retrying every tick.
        endSession("could not start a connection");
      }
      return;
  }
}

void BackgroundWifiCoordinator::requestSession(const char* reason) {
  if (!SETTINGS.smartNetworkEnabled() || !CrossPointSettings::supportsBatterySessions() ||
      !core::FeatureModules::hasCapability(core::Capability::BackgroundServer)) {
    return;
  }
  if (!sessionRequested_) {
    LOG_INF("BGCOORD", "Network session requested: %s", reason);
  }
  sessionRequested_ = true;
}

void BackgroundWifiCoordinator::endSession(const char* why) {
  if (sessionRequested_) {
    LOG_INF("BGCOORD", "Network session ended: %s", why);
  }
  sessionRequested_ = false;
  sessionStarted_ = false;
  wifiAutoConnectAttempted_ = false;
}

void BackgroundWifiCoordinator::reconcile(const BackgroundWifiReconcileContext& ctx) {
  if (sessionRequested_) {
    if (ctx.usbPowered && !BG_WIFI.isPendingOrRunning()) {
      // On USB the On-Charge server is the single owner; it serves while plugged in.
      endSession("USB power: the on-charge server takes over");
    } else if (sessionStarted_ && !BG_WIFI.isPendingOrRunning()) {
      // Linger expired, a reader took the radio, or the connect failed.
      endSession("bgwifi stopped");
    }
  }

  const background_server::ReconcileDecision decision =
      background_server::evaluateReconcile(background_server::ReconcileInput{
          .sessionWanted = sessionRequested_,
          .blockedByActivity = ctx.blockedByActivity,
          .usbBackgroundServerRunning = ctx.usbBackgroundServerRunning,
          .staConnected = hasStaWifiConnection(),
          .bgWifiRunning = BG_WIFI.isRunning(),
          .bgWifiPendingOrRunning = BG_WIFI.isPendingOrRunning(),
          .wifiAutoConnectAttempted = wifiAutoConnectAttempted_,
      });

  applyReconcileDecision(decision);
  if (sessionRequested_ && BG_WIFI.isPendingOrRunning()) {
    sessionStarted_ = true;
  }
}

bool BackgroundWifiCoordinator::beginTimedSleepAutoConnect(const char* logTag) {
  // A timer refresh only needs STA long enough to fetch its payload. Starting
  // BG_WIFI here also builds the full web server and mDNS route graph; tearing
  // those allocations down restores total heap but leaves the ESP32-C3 heap too
  // fragmented for PNGdec's single ~44 KB allocation.
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const std::optional<WifiCredential> cred = WIFI_STORE.findCredential(lastSsid);
  if (!cred) {
    LOG_WRN(logTag, "Timed WiFi connect has no saved last-connected credential");
    return false;
  }

  if (APP_STATE.wifiAutoConnectWaitingForNewCredential) {
    APP_STATE.wifiAutoConnectWaitingForNewCredential = false;
    if (!APP_STATE.saveToFile()) {
      LOG_WRN(logTag, "Failed to persist cleared WiFi credential recovery state");
    }
  }

  LOG_DBG(logTag, "Starting lightweight timed WiFi connection to: %s", lastSsid.c_str());
  WiFi.mode(WIFI_STA);
  const wl_status_t status =
      cred->password.empty() ? WiFi.begin(cred->ssid.c_str()) : WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  return status != WL_CONNECT_FAILED;
}

bool BackgroundWifiCoordinator::waitForStaConnection(const uint32_t timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;
  while (!hasStaWifiConnection() && millis() < deadline) {
    delay(50);
  }
  return hasStaWifiConnection();
}

void BackgroundWifiCoordinator::endTimedSleepWifi() {
  if (BG_WIFI.isRunning()) {
    // A timed refresh is about to render and return to deep sleep, so retaining
    // the STA radio only keeps WiFi allocations alive during the heaviest image
    // allocation. Fully stop it and then give the FreeRTOS idle task a bounded
    // window to reclaim the just-deleted WiFi and Terminus task stacks.
    BG_WIFI.stop();
  } else {
    // Lightweight timed connects have no BG_WIFI owner task. Tear the STA
    // driver down directly so its buffers are released before PNG rendering.
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
    delay(30);
    WifiScanCache::invalidate();
  }

  constexpr size_t kRenderAllocationBytes = 44 * 1024;
  constexpr size_t kRenderHeadroomBytes = 16 * 1024;
  constexpr unsigned long kCleanupWaitMs = 1000;
  const unsigned long cleanupStarted = millis();
  while (!heapguard::canAllocate(kRenderAllocationBytes, kRenderHeadroomBytes) &&
         millis() - cleanupStarted < kCleanupWaitMs) {
    delay(10);
  }

  if (!heapguard::canAllocate(kRenderAllocationBytes, kRenderHeadroomBytes)) {
    LOG_WRN("TREFRESH", "Timed WiFi cleanup left fragmented heap: free=%u largest=%u",
            static_cast<unsigned>(heapguard::freeBytes()), static_cast<unsigned>(heapguard::largestBlock()));
  } else {
    LOG_DBG("TREFRESH", "Timed WiFi cleanup ready for render: free=%u largest=%u",
            static_cast<unsigned>(heapguard::freeBytes()), static_cast<unsigned>(heapguard::largestBlock()));
  }
}

void BackgroundWifiCoordinator::releaseRadioForReader() {
  // An associated STA radio is not cheap on this heap. With Always mode keeping it
  // up through a book open (2026-10-26, X4), free heap fell to 28 KB mid-index --
  // under heapguard's 32 KB floor, so every chapter after the cover failed with
  // "Failed to load EPUB". The book also needs one contiguous 32 KB inflate window.
  // Closing the book requests the next network session (Smart mode).
  bool released = false;
  if (BG_WIFI.isPendingOrRunning()) {
    BG_WIFI.stop();
    released = true;
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
    delay(30);
    WifiScanCache::invalidate();
    released = true;
  }
  wifiAutoConnectAttempted_ = false;
  if (!released) {
    return;  // already off -- the second call from EpubReaderActivity::onEnter
  }

  // Deleted task stacks and driver buffers go back to the heap from the FreeRTOS
  // idle task, not from the calls above. Without this wait the inflate window was
  // measured 12 bytes short of its 32,768-byte run right after a teardown.
  constexpr size_t kInflateWindowBytes = 32768;  // InflateReader's INFLATE_DICT_SIZE
  constexpr unsigned long kReclaimWaitMs = 1000;
  const unsigned long started = millis();
  while (heapguard::largestBlock() <= kInflateWindowBytes && millis() - started < kReclaimWaitMs) {
    delay(10);
  }
  LOG_INF("BGCOORD", "Radio released for reader: free=%u largest=%u", static_cast<unsigned>(heapguard::freeBytes()),
          static_cast<unsigned>(heapguard::largestBlock()));
}

void BackgroundWifiCoordinator::onPrepareDeepSleep() {
  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop();
  }
  endSession("deep sleep");
}
