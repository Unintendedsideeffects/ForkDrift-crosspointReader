#include "network/BackgroundServerPolicy.h"

namespace background_server {

AutoConnectDecision evaluateAutoConnect(const AutoConnectInput& input) {
  AutoConnectDecision decision;

  if (!input.alwaysModeEnabled) {
    return decision;
  }
  if (input.waitingForNewCredential) {
    decision.action = AutoConnectAction::BlockedWaitingForCredential;
    return decision;
  }
  if (input.skipCount > 0) {
    decision.action = AutoConnectAction::SkipDueToBackoff;
    return decision;
  }
  if (input.lastConnectedSsid.empty()) {
    decision.action = AutoConnectAction::NoLastSsid;
    return decision;
  }
  if (!input.hasCredentialForLastSsid) {
    decision.action = AutoConnectAction::MissingCredentialForLastSsid;
    return decision;
  }

  decision.action = AutoConnectAction::StartWithLastCredential;
  return decision;
}

ReconcileDecision evaluateReconcile(const ReconcileInput& input) {
  ReconcileDecision decision;

  if (input.usbBackgroundServerRunning) {
    return decision;
  }

  if (!input.backgroundWifiEnabled || input.blockedByActivity) {
    if (input.bgWifiRunning) {
      decision.action = ReconcileAction::StopBgWifi;
      decision.stopKeepWifi = input.blockedByActivity;
    }
    return decision;
  }

  if (input.staConnected) {
    if (!input.bgWifiPendingOrRunning) {
      decision.action = ReconcileAction::StartUsingCurrentConnection;
    }
    return decision;
  }

  const bool autoConnectInFlight = input.bgWifiRunning && input.wifiAutoConnectAttempted;
  if (!autoConnectInFlight && input.bgWifiRunning) {
    decision.action = ReconcileAction::StopBgWifi;
    decision.stopKeepWifi = true;
    return decision;
  }

  if (!input.bgWifiPendingOrRunning) {
    decision.action = ReconcileAction::AttemptAutoConnect;
  }

  return decision;
}

bool shouldRunOnChargeBackgroundServer(const OnChargeServerInput& input) {
  return input.hasBackgroundServerCapability && input.backgroundServerOnCharge && input.usbConnected &&
         !input.blockedByActivity && !input.bgWifiRunning;
}

}  // namespace background_server
