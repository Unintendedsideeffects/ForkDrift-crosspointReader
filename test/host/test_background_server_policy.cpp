#include "CrossPointSettings.h"
#include "core/registries/HeapReclaimRegistry.h"
#include "doctest/doctest.h"
#include "network/background/BackgroundServerPolicy.h"

namespace {

background_server::AutoConnectInput makeAutoConnectInput(const bool connectWanted,
                                                         const bool waitingForNewCredential = false,
                                                         const char* lastConnectedSsid = "HomeNet",
                                                         const bool hasCredentialForLastSsid = true) {
  return background_server::AutoConnectInput{
      .connectWanted = connectWanted,
      .waitingForNewCredential = waitingForNewCredential,
      .lastConnectedSsid = lastConnectedSsid,
      .hasCredentialForLastSsid = hasCredentialForLastSsid,
  };
}

background_server::ReconcileInput makeReconcileInput(const bool sessionWanted, const bool blockedByActivity,
                                                     const bool usbBackgroundServerRunning, const bool staConnected,
                                                     const bool bgWifiRunning, const bool bgWifiPendingOrRunning,
                                                     const bool wifiAutoConnectAttempted) {
  return background_server::ReconcileInput{
      .sessionWanted = sessionWanted,
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

TEST_CASE("background server session requests WiFi auto-connect") {
  const auto decision = background_server::evaluateAutoConnect(makeAutoConnectInput(true));
  CHECK(decision.action == background_server::AutoConnectAction::StartWithLastCredential);
}

TEST_CASE("background server session waits for new credentials") {
  // Genuinely no credential for the last network: the latch must hold.
  const auto decision = background_server::evaluateAutoConnect(
      makeAutoConnectInput(true, true, "HomeNet", /*hasCredentialForLastSsid=*/false));
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
      background_server::evaluateAutoConnect(makeAutoConnectInput(true, /*waitingForNewCredential=*/true, "HomeNet",
                                                                  /*hasCredentialForLastSsid=*/true));
  CHECK(decision.action == background_server::AutoConnectAction::ClearStaleCredentialLatchAndStart);
}

TEST_CASE("background server: the latch holds when there is no last SSID to re-check against") {
  // An empty last-SSID cannot vouch for the credential, so this must not be
  // mistaken for a stale latch.
  const auto decision = background_server::evaluateAutoConnect(
      makeAutoConnectInput(true, /*waitingForNewCredential=*/true, "", /*hasCredentialForLastSsid=*/true));
  CHECK(decision.action == background_server::AutoConnectAction::BlockedWaitingForCredential);
}

TEST_CASE("background server: clearing a stale latch still respects always mode") {
  const auto decision = background_server::evaluateAutoConnect(
      makeAutoConnectInput(/*connectWanted=*/false, /*waitingForNewCredential=*/true, "HomeNet", true));
  CHECK(decision.action == background_server::AutoConnectAction::None);
}

TEST_CASE("background server on charge does not auto-connect WiFi") {
  const auto decision = background_server::evaluateAutoConnect(makeAutoConnectInput(false));
  CHECK(decision.action == background_server::AutoConnectAction::None);
}

TEST_CASE("background server on charge runs only on USB without blocking activity") {
  const background_server::OnChargeServerInput enabled{
      .hasBackgroundServerCapability = true,
      .smartEnabled = true,
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

TEST_CASE("background server session reconcile starts server on existing WiFi") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, true, false, false, false));
  CHECK(decision.action == background_server::ReconcileAction::StartUsingCurrentConnection);
}

TEST_CASE("background server session reconcile retries WiFi when disconnected") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, false, false, true));
  CHECK(decision.action == background_server::ReconcileAction::AttemptAutoConnect);
}

TEST_CASE("background server session reconcile keeps connect attempt in flight") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, true, true, true));
  CHECK(decision.action == background_server::ReconcileAction::None);
}

TEST_CASE("background server session reconcile stops stale task after failed connect") {
  const auto decision =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, true, false, false));
  CHECK(decision.action == background_server::ReconcileAction::StopBgWifi);
  CHECK(decision.stopKeepWifi);
}

TEST_CASE("background server setting is Off or Smart, defaulting to Smart") {
  if (!CrossPointSettings::supportsBackgroundServer()) {
    return;
  }
  CrossPointSettings& settings = CrossPointSettings::getInstance();
  const uint8_t saved = settings.backgroundServerMode;
  settings.backgroundServerMode = CrossPointSettings::BACKGROUND_SERVER_SMART;
  CHECK(settings.smartNetworkEnabled());
  settings.backgroundServerMode = CrossPointSettings::BACKGROUND_SERVER_OFF;
  CHECK_FALSE(settings.smartNetworkEnabled());
  settings.backgroundServerMode = saved;
}

TEST_CASE("Smart reconcile never connects without a requested session") {
  // No resident radio: with nothing requested, a disconnected device stays off.
  const auto idle =
      background_server::evaluateReconcile(makeReconcileInput(false, false, false, false, false, false, false));
  CHECK(idle.action == background_server::ReconcileAction::None);
  const auto requested =
      background_server::evaluateReconcile(makeReconcileInput(true, false, false, false, false, false, false));
  CHECK(requested.action == background_server::ReconcileAction::AttemptAutoConnect);
  // Home warming its carousel (or any blocker) defers the session, it does not drop it.
  const auto deferred =
      background_server::evaluateReconcile(makeReconcileInput(true, true, false, false, false, false, false));
  CHECK(deferred.action == background_server::ReconcileAction::None);
}

TEST_CASE("a session lingers two idle minutes, then powers the radio down") {
  using background_server::LingerAction;
  using background_server::SESSION_LINGER_IDLE_MS;
  CHECK(SESSION_LINGER_IDLE_MS == 120000u);
  CHECK(background_server::evaluateLinger(1000, 1000) == LingerAction::KeepServing);
  CHECK(background_server::evaluateLinger(1000 + SESSION_LINGER_IDLE_MS - 1, 1000) == LingerAction::KeepServing);
  CHECK(background_server::evaluateLinger(1000 + SESSION_LINGER_IDLE_MS, 1000) == LingerAction::EndSession);
  // millis() wrapped between the last request and now.
  CHECK(background_server::evaluateLinger(5000, 0xFFFFFF00u) == LingerAction::KeepServing);
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
  CHECK(background_server::startMinFreeBytes(kBgTaskStack) == kBgTaskStack + background_server::SERVER_STARTUP_BYTES +
                                                                  background_server::SERVER_SAFETY_FLOOR_BYTES +
                                                                  background_server::START_HEADROOM_BYTES);
  CHECK(background_server::startMinFreeBytes(kBgTaskStack) < 60000);
}

TEST_CASE("background start is allowed at the measured Home-resident heap after the route diet") {
  CHECK(verdictFor(33668, 9204) == background_server::StartResourceVerdict::Ok);
  CHECK(verdictFor(37348, 15860) == background_server::StartResourceVerdict::Ok);
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

// Deltas, not absolutes: HeapReclaimRegistry's entries[] and count are static
// with no clear(), so whatever other cases in this binary have registered is
// still there. Asserting empty() or an absolute call count silently depends on
// this case running first, which doctest only guarantees in declaration order
// (see plans/111c).
TEST_CASE("heap reclaim registry invokes each registered release once per releaseAll") {
  gReclaimCalls = 0;
  core::HeapReclaimRegistry::releaseAll();
  const int baseline = gReclaimCalls;  // counting entries registered by other cases

  core::HeapReclaimRegistry::add(core::HeapReclaimEntry{.name = "test cache", .release = &countingRelease});
  CHECK_FALSE(core::HeapReclaimRegistry::empty());

  gReclaimCalls = 0;
  core::HeapReclaimRegistry::releaseAll();
  CHECK(gReclaimCalls == baseline + 1);

  gReclaimCalls = 0;
  core::HeapReclaimRegistry::releaseAll();
  CHECK(gReclaimCalls == baseline + 1);  // repeatable, not cumulative
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

// The on-charge server (BackgroundWebServer) was migrated onto this same policy; it had
// its own hardcoded gates, 76000 to start and 48000 to keep running, against a measured
// startup cost of 16,336 bytes and an observed steady-state free heap of 43-55 KB. It
// therefore often could not start on charge at all, and tore itself down on ordinary dips.
// See docs/FINDINGS.md 2026-08-12T21:10Z.

TEST_CASE("background server: the on-charge server needs no task stack of its own") {
  // It runs on the main loop, so the derived gate is startup + safety floor + headroom.
  const uint32_t gate = background_server::startMinFreeBytes(0);
  CHECK(gate == background_server::SERVER_STARTUP_BYTES + background_server::SERVER_SAFETY_FLOOR_BYTES +
                    background_server::START_HEADROOM_BYTES);
  // Must be far below the 76000 it replaced, or the migration changed nothing.
  CHECK(gate < 76000);
  // ...and still comfortably above what the server actually spends.
  CHECK(gate > background_server::SERVER_STARTUP_BYTES);
}

TEST_CASE("background server: a running server is not re-charged for its startup cost") {
  // Charging startup again is what stopped healthy servers on an ordinary heap dip.
  CHECK(background_server::runningMinFreeBytes() < background_server::startMinFreeBytes(0));
  CHECK(background_server::runningMinFreeBytes() >= background_server::SERVER_SAFETY_FLOOR_BYTES);
}

TEST_CASE("background server: a measured running heap dip is not a fault") {
  using background_server::RunningHeapAction;
  CHECK(background_server::evaluateRunningHeap(10200) == RunningHeapAction::KeepServing);
  CHECK(background_server::evaluateRunningHeap(background_server::runningMinFreeBytes() - 1) ==
        RunningHeapAction::KeepServing);
  CHECK(background_server::evaluateRunningHeap(background_server::OBSERVED_RUNNING_MIN_FREE_BYTES) ==
        RunningHeapAction::KeepServing);
  CHECK(background_server::evaluateRunningHeap(background_server::OBSERVED_RUNNING_MIN_FREE_BYTES - 1) ==
        RunningHeapAction::StopKeepWifi);
  CHECK(background_server::runningAbortFreeBytes() == background_server::OBSERVED_RUNNING_MIN_FREE_BYTES);
  CHECK(background_server::runningAbortFreeBytes() < background_server::runningMinFreeBytes());
}

TEST_CASE("background server: observed steady-state heap can now start the on-charge server") {
  const auto verdict = background_server::evaluateStartResources(
      {.freeBytes = 43036, .largestContiguousBytes = 17396, .taskStackBytes = 0});
  CHECK(verdict == background_server::StartResourceVerdict::Ok);
}

TEST_CASE("background server start budget no longer charges deleted route handlers") {
  CHECK(background_server::SERVER_STARTUP_BYTES == 4504);
  constexpr uint32_t kBgWifiTaskStack = 8192;
  CHECK(background_server::startMinFreeBytes(kBgWifiTaskStack) == kBgWifiTaskStack + 4504 +
                                                                      background_server::SERVER_SAFETY_FLOOR_BYTES +
                                                                      background_server::START_HEADROOM_BYTES);
  CHECK(background_server::startMinFreeBytes(kBgWifiTaskStack) < 40912);
}

TEST_CASE("background server: observed Home idle can start Always after the route diet") {
  constexpr uint32_t kBgWifiTaskStack = 8192;
  const auto verdict = background_server::evaluateStartResources(
      {.freeBytes = 37348, .largestContiguousBytes = 15860, .taskStackBytes = kBgWifiTaskStack});
  CHECK(verdict == background_server::StartResourceVerdict::Ok);
  CHECK(background_server::startMinFreeBytes(kBgWifiTaskStack) < 37348);
}

TEST_CASE("background server: fragmentation still blocks a task-spawning start") {
  // Free heap alone is not enough for the Always path, which needs a contiguous 8 KB stack.
  const auto verdict = background_server::evaluateStartResources(
      {.freeBytes = 43036, .largestContiguousBytes = 4096, .taskStackBytes = 8192});
  CHECK(verdict == background_server::StartResourceVerdict::InsufficientContiguous);
}
