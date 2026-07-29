#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "network/wifi/BleWifiProvisioner.h"
#include "network/wifi/WifiScanCache.h"
#include "util/ButtonNavigator.h"

// WiFi selection states
enum class WifiSelectionState : uint8_t {
  RELEASING_BACKGROUND,  // Handing the radio back from the background service
  AUTO_CONNECTING,       // Trying to connect to the last known network
  SCANNING,              // Scanning for networks (nothing to show yet)
  NETWORK_LIST,          // Displaying available networks
  PASSWORD_ENTRY,        // Password keyboard subactivity is on screen
  MANUAL_SSID_ENTRY,     // SSID keyboard subactivity is on screen (hidden networks)
  BLE_PROVISIONING,      // Waiting for credentials over BLE
  CONNECTING,            // Attempting to connect
  CONNECTION_FAILED,     // Connection failed
  FORGET_PROMPT          // Asking user if they want to forget the network
};

/**
 * WifiSelectionActivity scans for access points and connects to one.
 *
 * Fast paths, in the order the entry policy considers them:
 *  - an existing STA link (typically left up by the background server) is
 *    adopted as-is and returned to the caller on the first frame;
 *  - a scan result younger than WifiScanCache::TTL_MS paints immediately while
 *    a refresh scan runs behind it;
 *  - otherwise a cold scan runs.
 *
 * Every waiting state (releasing, scanning, auto-connecting, connecting) is
 * cancellable with Back, and no radio operation blocks the main task — the
 * settling delays the WiFi driver needs between mode changes are driven from
 * loop() via RadioStep deadlines so the screen keeps painting and buttons keep
 * sampling.
 *
 * The result is a WifiResult on success, or a cancelled ActivityResult.
 */
class WifiSelectionActivity final : public Activity {
  // Deferred radio work. The driver needs settling time between disconnect,
  // mode change and begin/scan; running those as deadline-driven steps instead
  // of delay() keeps the UI responsive during the ~220 ms sequence.
  enum class RadioStep : uint8_t {
    None,
    ConnectReset,  // disconnect issued; next: mode(STA) + hard disconnect
    ConnectBegin,  // next: setHostname + WiFi.begin()
    ScanReset,     // disconnect issued; next: scanDelete + async scan
  };

  ButtonNavigator buttonNavigator;

  WifiSelectionState state = WifiSelectionState::SCANNING;
  int selectedNetworkIndex = 0;
  std::vector<WifiNetworkInfo> networks;

  RadioStep radioStep = RadioStep::None;
  unsigned long radioStepReadyAt = 0;

  // Selected network for connection
  std::string selectedSSID;
  bool selectedRequiresPassword = false;

  // Connection result
  std::string connectedIP;
  std::string connectionError;

  // Password to potentially save (from keyboard or saved credentials)
  std::string enteredPassword;

  // Cached MAC address string for the list footer
  std::string cachedMacAddress;

  // Whether the network was connected using an already-saved password. Only a
  // freshly entered password needs persisting on success.
  bool usedSavedPassword = false;

  // Whether to adopt an existing link / auto-connect on entry
  const bool allowAutoConnect;

  // True while a scan refreshes a list that is already on screen. The list
  // stays interactive; results are merged in when they land.
  bool refreshingInBackground = false;

  // A release was issued and the service was still running afterwards. The
  // service declines to force-delete a task that holds a mutex, so a retry
  // cannot succeed; proceed without the handoff instead of looping on stop().
  bool backgroundReleaseFailed = false;

  int forgetPromptSelection = 0;

  // Connection timeout
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 20000;
  static constexpr unsigned long CONNECT_FAILURE_GRACE_MS = 2500;
  static constexpr unsigned long RADIO_RESET_SETTLE_MS = 120;
  static constexpr unsigned long RADIO_MODE_SETTLE_MS = 100;
  unsigned long connectionStartTime = 0;
  unsigned long connectFailureGraceUntilMs = 0;

  // The first scan after STA power-up often fails (an in-flight SDK
  // auto-connect aborts it); retry silently before showing an empty list,
  // mirroring BackgroundWebServer's scanFailureBurst.
  //
  // The retries MUST go back through RadioStep::ScanReset with a growing
  // delay (see wifi_entry::evaluateScanRetry). Rescanning immediately from
  // the failure handler spends all three attempts inside a few milliseconds,
  // every one of them while the auto-connect that aborted the first scan is
  // still in flight — which is exactly how this screen regressed to showing an
  // empty network list despite "having retries".
  static constexpr uint8_t SCAN_RETRY_MAX = 3;
  // 150 / 300 / 600 ms — ~1.05 s of headroom worst case, invisible next to the
  // several seconds a scan itself takes.
  static constexpr unsigned long SCAN_RETRY_BASE_DELAY_MS = 150;
  uint8_t scanRetryCount = 0;
  BleWifiProvisioner bleProvisioner;

  // Rendering
  void renderNetworkList() const;
  void renderBleProvisioning() const;
  void renderStatusScreen() const;
  void renderConnectionFailed() const;
  void renderForgetPrompt() const;
  const char* statusTitle() const;
  std::string statusDetail() const;

  // Flow
  void evaluateEntry();
  void adoptExistingLink();
  void showCachedNetworks();
  void startWifiScan(bool keepListVisible);
  void processWifiScanResults();
  void serviceRadioStep();
  void selectRow(int index);
  void beginPasswordEntry();
  void beginManualSsidEntry();
  void startBleProvisioning();
  void checkBleProvisioning();
  void attemptConnection();
  void checkConnectionStatus();
  void abortConnection();
  void onConnected();

  // List model: the network rows are followed by a synthetic "Other network"
  // row so hidden SSIDs can be joined manually.
  int rowCount() const { return static_cast<int>(networks.size()) + 1; }
  bool isManualRow(const int index) const { return index == static_cast<int>(networks.size()); }
  const WifiNetworkInfo* selectedNetwork() const;

  void onComplete(bool connected);

 public:
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = true)
      : Activity("WifiSelection", renderer, mappedInput), allowAutoConnect(autoConnect) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksBackgroundServer() override { return true; }
};
