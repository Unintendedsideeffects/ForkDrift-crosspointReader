#include "network/background/BackgroundServerPolicy.h"

namespace background_server {

bool canStart(const ServiceState state) { return state == ServiceState::Stopped; }

ServiceState requestStop(const ServiceState state) {
  return state == ServiceState::Running ? ServiceState::StopRequested : state;
}

ServiceState noteStopTimeout(const ServiceState state) {
  return state == ServiceState::StopRequested ? ServiceState::Wedged : state;
}

ServiceState noteCleanupComplete(const ServiceState) { return ServiceState::Stopped; }

uint32_t startMinFreeBytes(const uint32_t taskStackBytes) {
  return taskStackBytes + SERVER_STARTUP_BYTES + SERVER_SAFETY_FLOOR_BYTES + START_HEADROOM_BYTES;
}

uint32_t runningMinFreeBytes() { return SERVER_SAFETY_FLOOR_BYTES + RUNNING_HEADROOM_BYTES; }

StartResourceVerdict evaluateStartResources(const StartResourceInput& input) {
  if (input.freeBytes < startMinFreeBytes(input.taskStackBytes)) {
    return StartResourceVerdict::InsufficientFree;
  }
  // Checked second so the free-heap shortfall — the one a caller can act on by
  // releasing a cache — is reported in preference to the fragmentation one.
  if (input.largestContiguousBytes < input.taskStackBytes) {
    return StartResourceVerdict::InsufficientContiguous;
  }
  return StartResourceVerdict::Ok;
}

AutoConnectDecision evaluateAutoConnect(const AutoConnectInput& input) {
  AutoConnectDecision decision;

  if (!input.alwaysModeEnabled) {
    return decision;
  }
  if (input.waitingForNewCredential) {
    // The latch records a conclusion ("there is no usable credential") that can
    // be re-derived, so it must be re-checked rather than trusted. A credential
    // for the last network being present again means the latch is stale.
    if (input.hasCredentialForLastSsid && !input.lastConnectedSsid.empty()) {
      decision.action = AutoConnectAction::ClearStaleCredentialLatchAndStart;
      return decision;
    }
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
