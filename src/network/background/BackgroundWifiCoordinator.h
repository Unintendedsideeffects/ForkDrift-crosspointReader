#pragma once

#include <cstdint>

#include "network/background/BackgroundServerPolicy.h"

struct BackgroundWifiReconcileContext {
  bool blockedByActivity = false;
  bool usbBackgroundServerRunning = false;
  bool usbPowered = false;
};

// Owns the radio for Smart network sessions on battery. A session is requested
// (closing a book), runs bgwifi once nothing blocks it -- due jobs, then the server
// lingers until SESSION_LINGER_IDLE_MS of idle -- and ends when bgwifi stops. It is
// never retried in a loop and there is no resident radio. On USB power the
// On-Charge BackgroundWebServer is the single owner instead.
class BackgroundWifiCoordinator {
  static BackgroundWifiCoordinator instance;

  bool wifiAutoConnectAttempted_ = false;
  bool sessionRequested_ = false;
  bool sessionStarted_ = false;
  // reconcile() runs every main-loop tick, so the auto-connect skip reasons are
  // logged only when the reason CHANGES. Logging them unconditionally floods the
  // serial log — and with developer mode on it appends to the SD debug file on
  // every tick, which is an SD-wear and latency problem, not just noise.
  int lastAutoConnectLoggedAction_ = -1;

  background_server::AutoConnectInput buildAutoConnectInput() const;
  bool attemptAutoConnect(const char* logTag);
  void applyReconcileDecision(const background_server::ReconcileDecision& decision);
  void endSession(const char* why);

 public:
  BackgroundWifiCoordinator() = default;
  BackgroundWifiCoordinator(const BackgroundWifiCoordinator&) = delete;
  BackgroundWifiCoordinator& operator=(const BackgroundWifiCoordinator&) = delete;

  static BackgroundWifiCoordinator& getInstance() { return instance; }

  void reconcile(const BackgroundWifiReconcileContext& ctx);
  // Ask for one battery session (Smart mode only; ignored otherwise).
  void requestSession(const char* reason);
  // True from the request until bgwifi has stopped again.
  bool sessionActive() const { return sessionRequested_; }
  bool beginTimedSleepAutoConnect(const char* logTag);
  bool waitForStaConnection(uint32_t timeoutMs);
  void endTimedSleepWifi();
  // Reading owns the radio's heap. Stops bgwifi and powers the STA radio down
  // before a book loads; closing the book requests the next session.
  void releaseRadioForReader();
  void onPrepareDeepSleep();
};
