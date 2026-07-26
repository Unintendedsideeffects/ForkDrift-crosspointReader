#include "CrossPointSettings.h"
#include "doctest/doctest.h"
#include "network/background/BackgroundServerPolicy.h"

namespace {

background_server::AutoConnectInput makeAutoConnectInput(const bool alwaysModeEnabled,
                                                         const bool waitingForNewCredential = false,
                                                         const uint8_t skipCount = 0,
                                                         const char* lastConnectedSsid = "HomeNet",
                                                         const bool hasCredentialForLastSsid = true) {
  return background_server::AutoConnectInput{
      .alwaysModeEnabled = alwaysModeEnabled,
      .waitingForNewCredential = waitingForNewCredential,
      .skipCount = skipCount,
      .lastConnectedSsid = lastConnectedSsid,
      .hasCredentialForLastSsid = hasCredentialForLastSsid,
  };
}

background_server::ReconcileInput makeReconcileInput(const bool backgroundWifiEnabled, const bool blockedByActivity,
                                                     const bool usbBackgroundServerRunning, const bool staConnected,
                                                     const bool bgWifiRunning, const bool bgWifiPendingOrRunning,
                                                     const bool wifiAutoConnectAttempted) {
  return background_server::ReconcileInput{
      .backgroundWifiEnabled = backgroundWifiEnabled,
      .blockedByActivity = blockedByActivity,
      .usbBackgroundServerRunning = usbBackgroundServerRunning,
      .staConnected = staConnected,
      .bgWifiRunning = bgWifiRunning,
      .bgWifiPendingOrRunning = bgWifiPendingOrRunning,
      .wifiAutoConnectAttempted = wifiAutoConnectAttempted,
  };
}

}  // namespace

TEST_CASE("background service lifecycle admits restart only after owner cleanup") {
  using background_server::ServiceState;

  CHECK(background_server::canStart(ServiceState::Stopped));
  CHECK_FALSE(background_server::canStart(ServiceState::Running));
  CHECK_FALSE(background_server::canStart(ServiceState::StopRequested));
  CHECK_FALSE(background_server::canStart(ServiceState::Wedged));

  CHECK(background_server::requestStop(ServiceState::Running) == ServiceState::StopRequested);
  CHECK(background_server::noteStopTimeout(ServiceState::StopRequested) == ServiceState::Wedged);
  CHECK_FALSE(background_server::canStart(ServiceState::Wedged));
  CHECK(background_server::noteCleanupComplete(ServiceState::Wedged) == ServiceState::Stopped);
  CHECK(background_server::canStart(background_server::noteCleanupComplete(ServiceState::StopRequested)));
}

TEST_CASE("background service lifecycle ignores invalid duplicate transitions") {
  using background_server::ServiceState;

  CHECK(background_server::requestStop(ServiceState::Stopped) == ServiceState::Stopped);
  CHECK(background_server::requestStop(ServiceState::Wedged) == ServiceState::Wedged);
  CHECK(background_server::noteStopTimeout(ServiceState::Running) == ServiceState::Running);
  CHECK(background_server::noteStopTimeout(ServiceState::Wedged) == ServiceState::Wedged);
}

TEST_CASE("background server always mode requests WiFi auto-connect") {
  const auto decision = background_server::evaluateAutoConnect(makeAutoConnectInput(true));
  CHECK(decision.action == background_server::AutoConnectAction::StartWithLastCredential);
}

TEST_CASE("background server always mode respects auto-connect backoff") {
  const auto decision = background_server::evaluateAutoConnect(makeAutoConnectInput(true, false, 2));
  CHECK(decision.action == background_server::AutoConnectAction::SkipDueToBackoff);
}

TEST_CASE("background server always mode waits for new credentials") {
  const auto decision = background_server::evaluateAutoConnect(makeAutoConnectInput(true, true));
  CHECK(decision.action == background_server::AutoConnectAction::BlockedWaitingForCredential);
}

TEST_CASE("background server on charge does not auto-connect WiFi") {
  const auto decision = background_server::evaluateAutoConnect(makeAutoConnectInput(false));
  CHECK(decision.action == background_server::AutoConnectAction::None);
}

TEST_CASE("background server on charge runs only on USB without blocking activity") {
  const background_server::OnChargeServerInput enabled{
      .hasBackgroundServerCapability = true,
      .backgroundServerOnCharge = true,
      .usbConnected = true,
      .blockedByActivity = false,
      .bgWifiRunning = false,
  };
  CHECK(background_server::shouldRunOnChargeBackgroundServer(enabled));

  background_server::OnChargeServerInput blocked = enabled;
  blocked.blockedByActivity = true;
  CHECK_FALSE(background_server::shouldRunOnChargeBackgroundServer(blocked));

  background_server::OnChargeServerInput unplugged = enabled;
  unplugged.usbConnected = false;
  CHECK_FALSE(background_server::shouldRunOnChargeBackgroundServer(unplugged));

  background_server::OnChargeServerInput wifiBusy = enabled;
  wifiBusy.bgWifiRunning = true;
  CHECK_FALSE(background_server::shouldRunOnChargeBackgroundServer(wifiBusy));
}

TEST_CASE("background server always reconcile starts server on existing WiFi") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, true, false, false, false));
  CHECK(decision.action == background_server::ReconcileAction::StartUsingCurrentConnection);
}

TEST_CASE("background server always reconcile retries WiFi when disconnected") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, false, false, true));
  CHECK(decision.action == background_server::ReconcileAction::AttemptAutoConnect);
}

TEST_CASE("background server always reconcile keeps connect attempt in flight") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, true, true, true));
  CHECK(decision.action == background_server::ReconcileAction::None);
}

TEST_CASE("background server always reconcile stops stale task after failed connect") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, true, false, false));
  CHECK(decision.action == background_server::ReconcileAction::StopBgWifi);
  CHECK(decision.stopKeepWifi);
}

TEST_CASE("background server settings map always mode to wifi auto-connect") {
  if (!CrossPointSettings::supportsBackgroundServerAlwaysMode()) {
    return;
  }

  CrossPointSettings& settings = CrossPointSettings::getInstance();
  settings.setBackgroundServerMode(CrossPointSettings::BACKGROUND_SERVER_ALWAYS);
  CHECK(settings.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_ALWAYS);
  CHECK(settings.keepsBackgroundServerOnWifiWhileAwake());
  CHECK(settings.wifiAutoConnect == 1);
  CHECK(settings.backgroundServerOnCharge == 1);
}

TEST_CASE("background server settings map on charge mode without wifi auto-connect") {
  if (!CrossPointSettings::supportsBackgroundServerOnChargeMode()) {
    return;
  }

  CrossPointSettings& settings = CrossPointSettings::getInstance();
  settings.setBackgroundServerMode(CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE);
  CHECK(settings.getBackgroundServerMode() == CrossPointSettings::BACKGROUND_SERVER_ON_CHARGE);
  CHECK_FALSE(settings.keepsBackgroundServerOnWifiWhileAwake());
  CHECK(settings.wifiAutoConnect == 0);
  CHECK(settings.backgroundServerOnCharge == 1);
}
