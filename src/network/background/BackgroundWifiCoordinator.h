#pragma once

#include <cstdint>

#include "network/background/BackgroundServerPolicy.h"

struct BackgroundWifiReconcileContext {
  bool blockedByActivity = false;
  bool usbBackgroundServerRunning = false;
};

class BackgroundWifiCoordinator {
  static BackgroundWifiCoordinator instance;

  bool wifiAutoConnectAttempted_ = false;

  enum class AlwaysBgServerState {
    Unknown,
    Running,
    Backoff,
    IdleNoCredential,
    Stopped,
  };

  AlwaysBgServerState lastAlwaysModeState_ = AlwaysBgServerState::Unknown;
  // reconcile() runs every main-loop tick, so the auto-connect skip reasons are
  // logged only when the reason CHANGES. Logging them unconditionally floods the
  // serial log — and with developer mode on it appends to the SD debug file on
  // every tick, which is an SD-wear and latency problem, not just noise.
  int lastAutoConnectLoggedAction_ = -1;

  background_server::AutoConnectInput buildAutoConnectInput(bool explicitRequest, bool ignoreBootBackoff) const;
  bool attemptAutoConnect(const char* logTag, bool explicitRequest = false, bool ignoreBootBackoff = false);
  void applyReconcileDecision(const background_server::ReconcileDecision& decision);
  void logAlwaysModeStateTransition();

 public:
  BackgroundWifiCoordinator() = default;
  BackgroundWifiCoordinator(const BackgroundWifiCoordinator&) = delete;
  BackgroundWifiCoordinator& operator=(const BackgroundWifiCoordinator&) = delete;

  static BackgroundWifiCoordinator& getInstance() { return instance; }

  void reconcile(const BackgroundWifiReconcileContext& ctx);
  void attemptBootAutoConnect();
  bool beginTimedSleepAutoConnect(const char* logTag);
  bool waitForStaConnection(uint32_t timeoutMs);
  void endTimedSleepWifi();
  void onPrepareDeepSleep();
};
