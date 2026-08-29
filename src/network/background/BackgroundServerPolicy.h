#pragma once

#include <cstdint>
#include <string_view>

namespace background_server {

enum class ServiceState : uint8_t {
  Stopped,
  Running,
  StopRequested,
  Wedged,
};

bool canStart(ServiceState state);
ServiceState requestStop(ServiceState state);
ServiceState noteStopTimeout(ServiceState state);
ServiceState noteCleanupComplete(ServiceState state);

enum class AutoConnectAction {
  None,
  BlockedWaitingForCredential,
  SkipDueToBackoff,
  StartWithLastCredential,
  NoLastSsid,
  MissingCredentialForLastSsid,
  // The "waiting for a new credential" latch is set, but a credential for the
  // last network exists again — so the latch is stale. Clear it (persisting the
  // change) and connect. Without this the latch is a one-way door: only
  // WifiCredentialStore::addCredential() clears it, and that runs *only* when
  // the user types a password, so reconnecting to an already-saved network
  // proves the credential works yet leaves auto-connect disabled forever.
  ClearStaleCredentialLatchAndStart,
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

// Occupancy diet 2026-08-29: one RequestHandler + flash table. Budget is the
// 2026-07-30 remainder after dropping the 11,832-byte route line (WebServer 304
// + deferred WS/UDP 4,200). Always starts from measured Home idle (~37 KB).
constexpr uint32_t SERVER_STARTUP_BYTES = 4504;
constexpr uint32_t SERVER_SAFETY_FLOOR_BYTES = 12 * 1024;
// Not measured: allocator bookkeeping and small request-time spikes. Named so a
// future reader can tell the measured part from the judgement part.
constexpr uint32_t START_HEADROOM_BYTES = 4096;

enum class StartResourceVerdict : uint8_t {
  Ok,
  InsufficientFree,
  InsufficientContiguous,
};

struct StartResourceInput {
  uint32_t freeBytes = 0;
  // ESP.getMaxAllocHeap(): the largest single run, NOT a share of freeBytes.
  uint32_t largestContiguousBytes = 0;
  // Passed in rather than duplicated here so BackgroundWifiService::TASK_STACK
  // stays the single source of truth for its own stack size.
  uint32_t taskStackBytes = 0;
};

// Two checks, not one. The task stack is a single contiguous allocation, and
// ESP.getFreeHeap() is a sum that says nothing about the largest run. Measured
// with Home resident: free = 33,668 but largest contiguous = 9,204 — a
// free-heap-only gate passes and xTaskCreate then fails.
uint32_t startMinFreeBytes(uint32_t taskStackBytes);
StartResourceVerdict evaluateStartResources(const StartResourceInput& input);

// Operating floor for a server that is ALREADY running. This is not a teardown trigger:
// on-charge RUNNING free sits at ~10,200, which is below this number, so using it to stop
// the server fires in the normal state. Per-request spikes are each handler's own
// responsibility (library shelf refresh uses LibraryShelfRefreshPolicy, 38 KB HTTPS floor).
constexpr uint32_t RUNNING_HEADROOM_BYTES = 4096;
uint32_t runningMinFreeBytes();

// Last-resort abort for a running server. Device walk 2026-08-29, on-charge serving:
// getMinFreeHeap() ranged 4,860–6,180 while typical free sat at ~10,200. The abort sits
// at the exclusive lower bound of that watermark so it cannot fire in a state already
// measured as survivable. The action is stop(keepWifi), never the WAIT_RETRY fault path:
// that disconnects WiFi, and re-association is itself heap-churning.
constexpr uint32_t OBSERVED_RUNNING_MIN_FREE_BYTES = 4860;
uint32_t runningAbortFreeBytes();

enum class RunningHeapAction : uint8_t {
  KeepServing,
  StopKeepWifi,
};

RunningHeapAction evaluateRunningHeap(uint32_t freeBytes);

struct OnChargeServerInput {
  bool hasBackgroundServerCapability = false;
  bool backgroundServerOnCharge = false;
  bool usbConnected = false;
  bool blockedByActivity = false;
  bool bgWifiRunning = false;
};

bool shouldRunOnChargeBackgroundServer(const OnChargeServerInput& input);

}  // namespace background_server
