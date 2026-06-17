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

  background_server::AutoConnectInput buildAutoConnectInput() const;
  bool attemptAutoConnect(const char* logTag);
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
