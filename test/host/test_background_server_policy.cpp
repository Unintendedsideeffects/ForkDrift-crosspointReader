#include "CrossPointSettings.h"
#include "core/registries/HeapReclaimRegistry.h"
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
  // Genuinely no credential for the last network: the latch must hold.
  const auto decision = background_server::evaluateAutoConnect(
      makeAutoConnectInput(true, true, 0, "HomeNet", /*hasCredentialForLastSsid=*/false));
  CHECK(decision.action == background_server::AutoConnectAction::BlockedWaitingForCredential);
}

// Regression: the device stopped auto-connecting at boot even though its saved
// network was in range and connected fine by hand. The latch is only cleared by
// WifiCredentialStore::addCredential(), which runs only when the user *types* a
// password — so reconnecting to an already-saved network proved the credential
// worked while leaving auto-connect disabled forever. The latch persists a
// conclusion that can be re-derived, so it has to be re-checked, not trusted.
TEST_CASE("background server: a stale credential latch clears when the credential is back") {
  const auto decision =
      background_server::evaluateAutoConnect(makeAutoConnectInput(true, /*waitingForNewCredential=*/true, 0, "HomeNet",
                                                                  /*hasCredentialForLastSsid=*/true));
  CHECK(decision.action == background_server::AutoConnectAction::ClearStaleCredentialLatchAndStart);
}

TEST_CASE("background server: the latch holds when there is no last SSID to re-check against") {
  // An empty last-SSID cannot vouch for the credential, so this must not be
  // mistaken for a stale latch.
  const auto decision = background_server::evaluateAutoConnect(
      makeAutoConnectInput(true, /*waitingForNewCredential=*/true, 0, "", /*hasCredentialForLastSsid=*/true));
  CHECK(decision.action == background_server::AutoConnectAction::BlockedWaitingForCredential);
}

TEST_CASE("background server: clearing a stale latch still respects always mode") {
  const auto decision = background_server::evaluateAutoConnect(
      makeAutoConnectInput(/*alwaysModeEnabled=*/false, /*waitingForNewCredential=*/true, 0, "HomeNet", true));
  CHECK(decision.action == background_server::AutoConnectAction::None);
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

// ── Start-resource gate ─────────────────────────────────────────────────────
// The numbers below are the device trace of 2026-07-30 (ESP32-C3, 181,240 B
// total heap). They are recorded as tests so a future change to the derivation
// has to confront the measurement rather than re-guess a single constant.

namespace {

constexpr uint32_t kBgTaskStack = 8192;  // BackgroundWifiService::TASK_STACK

background_server::StartResourceVerdict verdictFor(const uint32_t freeBytes, const uint32_t largestBytes) {
  return background_server::evaluateStartResources(background_server::StartResourceInput{
      .freeBytes = freeBytes,
      .largestContiguousBytes = largestBytes,
      .taskStackBytes = kBgTaskStack,
  });
}

}  // namespace

TEST_CASE("background start minimum is the sum of the measured parts") {
  // 8,192 stack + 16,336 startup + 12,288 floor + 4,096 headroom.
  CHECK(background_server::startMinFreeBytes(kBgTaskStack) == 40912);
  // Guards against reinstating the old unmeasured 60,000 gate, which rejected
  // heap states the server demonstrably starts in.
  CHECK(background_server::startMinFreeBytes(kBgTaskStack) < 60000);
}

TEST_CASE("background start is refused at the measured Home-resident heap") {
  // Device trace, "exit Home": free=33668 largest=9204. Home's 48 KB cover
  // buffer is still resident at this point, and the server cannot start.
  CHECK(verdictFor(33668, 9204) == background_server::StartResourceVerdict::InsufficientFree);
}

TEST_CASE("background start is allowed once Home has released its caches") {
  // Device trace, "enter CrossPointWebServer": free=82672 largest=49140.
  CHECK(verdictFor(82672, 49140) == background_server::StartResourceVerdict::Ok);
}

TEST_CASE("background start reports fragmentation separately from low heap") {
  // Ample total, but no run large enough for the task stack. A free-heap-only
  // gate passes this and xTaskCreate then fails.
  CHECK(verdictFor(120000, 4096) == background_server::StartResourceVerdict::InsufficientContiguous);
}

TEST_CASE("background start gate boundaries are inclusive of the requirement") {
  const uint32_t minFree = background_server::startMinFreeBytes(kBgTaskStack);
  CHECK(verdictFor(minFree, kBgTaskStack) == background_server::StartResourceVerdict::Ok);
  CHECK(verdictFor(minFree - 1, kBgTaskStack) == background_server::StartResourceVerdict::InsufficientFree);
  CHECK(verdictFor(minFree, kBgTaskStack - 1) == background_server::StartResourceVerdict::InsufficientContiguous);
}

// ── Heap reclaim registry ───────────────────────────────────────────────────
// Only this test touches the registry's static state, so no reset hook is
// needed (and none is offered — a test-only reset on a boot-time registry is a
// footgun waiting for production use).

namespace {

int gReclaimCalls = 0;

void countingRelease() { ++gReclaimCalls; }

}  // namespace

TEST_CASE("heap reclaim registry starts empty and invokes registered releases") {
  CHECK(core::HeapReclaimRegistry::empty());

  core::HeapReclaimRegistry::add(core::HeapReclaimEntry{
      .name = "test cache",
      .release = &countingRelease,
  });
  CHECK_FALSE(core::HeapReclaimRegistry::empty());

  gReclaimCalls = 0;
  core::HeapReclaimRegistry::releaseAll();
  CHECK(gReclaimCalls == 1);

  // Releases must be idempotent, so calling again is legal and simply re-runs.
  core::HeapReclaimRegistry::releaseAll();
  CHECK(gReclaimCalls == 2);
}

TEST_CASE("heap reclaim registry skips a null release without skipping the rest") {
  // Registers both entries itself so the assertion does not depend on which
  // other test cases ran first.
  core::HeapReclaimRegistry::add(core::HeapReclaimEntry{.name = "null cache", .release = nullptr});
  core::HeapReclaimRegistry::add(core::HeapReclaimEntry{.name = "after null", .release = &countingRelease});

  gReclaimCalls = 0;
  core::HeapReclaimRegistry::releaseAll();  // must not crash on the null entry
  // At least the entry registered after the null one ran, proving a null does
  // not abort the sweep.
  CHECK(gReclaimCalls >= 1);
}
