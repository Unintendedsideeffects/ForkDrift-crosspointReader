#pragma once

#include <cstdint>
#include <string_view>

namespace background_server {

enum class AutoConnectAction {
  None,
  BlockedWaitingForCredential,
  SkipDueToBackoff,
  StartWithLastCredential,
  NoLastSsid,
  MissingCredentialForLastSsid,
};

struct AutoConnectInput {
  bool alwaysModeEnabled = false;
  bool waitingForNewCredential = false;
  uint8_t skipCount = 0;
  std::string_view lastConnectedSsid;
  bool hasCredentialForLastSsid = false;
};

struct AutoConnectDecision {
  AutoConnectAction action = AutoConnectAction::None;
};

AutoConnectDecision evaluateAutoConnect(const AutoConnectInput& input);

enum class ReconcileAction {
  None,
  StopBgWifi,
  StartUsingCurrentConnection,
  AttemptAutoConnect,
};

struct ReconcileInput {
  bool backgroundWifiEnabled = false;
  bool blockedByActivity = false;
  bool usbBackgroundServerRunning = false;
  bool staConnected = false;
  bool bgWifiRunning = false;
  bool bgWifiPendingOrRunning = false;
  bool wifiAutoConnectAttempted = false;
};

struct ReconcileDecision {
  ReconcileAction action = ReconcileAction::None;
  bool stopKeepWifi = false;
};

ReconcileDecision evaluateReconcile(const ReconcileInput& input);

struct OnChargeServerInput {
  bool hasBackgroundServerCapability = false;
  bool backgroundServerOnCharge = false;
  bool usbConnected = false;
  bool blockedByActivity = false;
  bool bgWifiRunning = false;
};

bool shouldRunOnChargeBackgroundServer(const OnChargeServerInput& input);

}  // namespace background_server
