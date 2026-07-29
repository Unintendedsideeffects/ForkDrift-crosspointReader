#include "WifiSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"
#include "network/background/BackgroundWifiService.h"
#include "network/wifi/WifiEntryPolicy.h"
#include "network/wifi/WifiScanCache.h"
#include "network/wifi/WifiUtil.h"
#include "util/NetworkNames.h"
#include "util/WifiCredentialStore.h"
#include "util/WifiScanPolicy.h"

namespace {
// Not MAX_SSID_LEN / MAX_PASSWORD_LEN: both are ESP-IDF macros
// (esp_wifi_types_generic.h) and would be substituted before compilation.
constexpr size_t SSID_MAX_CHARS = 32;
constexpr size_t PASSWORD_MAX_CHARS = 64;

// 0..4 bars from RSSI. Matches the thresholds CrossPointWebServerActivity uses
// for its header indicator so the same network reads the same everywhere.
int barsForRssi(const int32_t rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

std::string signalLabel(const int32_t rssi) {
  const int bars = barsForRssi(rssi);
  std::string label;
  label.reserve(4);
  for (int i = 0; i < 4; i++) {
    label += (i < bars) ? '|' : '.';
  }
  return label;
}
}  // namespace

// ── Lifecycle ───────────────────────────────────────────────────────────────

void WifiSelectionActivity::onEnter() {
  Activity::onEnter();

  WIFI_STORE.loadFromFile();

  selectedNetworkIndex = 0;
  networks.clear();
  selectedSSID.clear();
  connectedIP.clear();
  connectionError.clear();
  enteredPassword.clear();
  usedSavedPassword = false;
  forgetPromptSelection = 0;
  refreshingInBackground = false;
  backgroundReleaseFailed = false;
  radioStep = RadioStep::None;

  // Cache MAC address for the list footer
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[64];
  snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  cachedMacAddress = macStr;

  // Paint the "preparing" screen before doing anything that can block: stop()
  // waits for the background task to leave its request handler, which is
  // normally instant but can take seconds mid-upload.
  if (BG_WIFI.isRunning()) {
    LOG_DBG("WIFISEL", "Releasing background WiFi service for foreground use");
    state = WifiSelectionState::RELEASING_BACKGROUND;
    requestUpdate();
    return;
  }

  evaluateEntry();
}

void WifiSelectionActivity::onExit() {
  Activity::onExit();
  bleProvisioner.stop();
  WiFi.scanDelete();
}

// ── Entry policy ────────────────────────────────────────────────────────────

void WifiSelectionActivity::evaluateEntry() {
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const bool hasLastCredential = !lastSsid.empty() && WIFI_STORE.findCredential(lastSsid) != nullptr;

  const wifi_entry::EntryAction action = wifi_entry::evaluateEntry(wifi_entry::EntryInput{
      .linkUp = hasStaWifiConnection(),
      .backgroundServiceRunning = BG_WIFI.isRunning(),
      .allowAutoConnect = allowAutoConnect,
      .hasLastCredential = hasLastCredential,
      .scanCacheFresh = WifiScanCache::isFresh(),
      .backgroundReleaseFailed = backgroundReleaseFailed,
  });

  switch (action) {
    case wifi_entry::EntryAction::ReleaseBackgroundService:
      state = WifiSelectionState::RELEASING_BACKGROUND;
      requestUpdate();
      return;

    case wifi_entry::EntryAction::AdoptExistingLink:
      adoptExistingLink();
      return;

    case wifi_entry::EntryAction::AutoConnectLast: {
      const WifiCredential* cred = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
      if (cred == nullptr) {  // Raced with a forget; fall back to a scan.
        startWifiScan(false);
        return;
      }
      LOG_DBG("WIFISEL", "Auto-connecting to %s", cred->ssid.c_str());
      selectedSSID = cred->ssid;
      enteredPassword = cred->password;
      selectedRequiresPassword = !cred->password.empty();
      usedSavedPassword = true;
      state = WifiSelectionState::AUTO_CONNECTING;
      attemptConnection();
      return;
    }

    case wifi_entry::EntryAction::ShowCachedNetworks:
      showCachedNetworks();
      return;

    case wifi_entry::EntryAction::Scan:
      startWifiScan(false);
      return;
  }
}

void WifiSelectionActivity::adoptExistingLink() {
  selectedSSID = WiFi.SSID().c_str();
  connectedIP = WiFi.localIP().toString().c_str();
  usedSavedPassword = true;
  LOG_DBG("WIFISEL", "Adopting existing link: %s (%s)", selectedSSID.c_str(), connectedIP.c_str());
  onConnected();
}

void WifiSelectionActivity::showCachedNetworks() {
  WifiScanCache::refreshSavedFlags();
  networks = WifiScanCache::entries();
  selectedNetworkIndex = 0;
  state = WifiSelectionState::NETWORK_LIST;
  requestUpdate();

  // The list is already usable; refresh it behind the user's back so a network
  // that appeared since the cached scan shows up without them asking.
  startWifiScan(true);
}

// ── Scanning ────────────────────────────────────────────────────────────────

void WifiSelectionActivity::startWifiScan(const bool keepListVisible) {
  bleProvisioner.stop();
  scanRetryCount = 0;
  refreshingInBackground = keepListVisible;

  if (!keepListVisible) {
    networks.clear();
    selectedNetworkIndex = 0;
    state = WifiSelectionState::SCANNING;
  }
  requestUpdate();

  // Set WiFi mode to station. The SDK starts an NVS auto-connect on STA
  // power-up which aborts a concurrent scan, so kill it before scanning.
  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  radioStep = RadioStep::ScanReset;
  radioStepReadyAt = millis() + RADIO_MODE_SETTLE_MS;
}

void WifiSelectionActivity::processWifiScanResults() {
  const int16_t scanResult = WiFi.scanComplete();

  if (scanResult == WIFI_SCAN_RUNNING) {
    return;
  }

  if (scanResult == WIFI_SCAN_FAILED) {
    const wifi_entry::ScanRetryDecision decision = wifi_entry::evaluateScanRetry(wifi_entry::ScanRetryInput{
        .failureCount = static_cast<uint8_t>(scanRetryCount + 1),
        .maxRetries = SCAN_RETRY_MAX,
        .baseDelayMs = SCAN_RETRY_BASE_DELAY_MS,
    });
    if (decision.retry) {
      scanRetryCount++;
      LOG_DBG("WIFISEL", "Scan failed; retrying (%u/%u) in %lums", scanRetryCount, SCAN_RETRY_MAX, decision.delayMs);
      WiFi.scanDelete();
      // Re-arm the radio and wait, exactly as the first scan does. Calling
      // startWifiScanAsync() straight from here (as this did previously) skips
      // both, so all retries fire within the same handful of milliseconds while
      // the SDK auto-connect that aborted the scan is still running — the
      // budget is spent before the radio is ever free.
      WiFi.disconnect(false, true);
      radioStep = RadioStep::ScanReset;
      radioStepReadyAt = millis() + decision.delayMs;
      return;
    }
    LOG_ERR("WIFISEL", "WiFi scan failed %u times; showing empty list", static_cast<unsigned>(SCAN_RETRY_MAX) + 1);
    WiFi.scanDelete();
    refreshingInBackground = false;
    state = WifiSelectionState::NETWORK_LIST;
    requestUpdate();
    return;
  }

  // Remember which network was selected so a background refresh does not move
  // the cursor out from under the user.
  std::string previousSelection;
  if (state == WifiSelectionState::NETWORK_LIST && !isManualRow(selectedNetworkIndex) &&
      selectedNetworkIndex < static_cast<int>(networks.size())) {
    previousSelection = networks[selectedNetworkIndex].ssid;
  }

  std::vector<WifiNetworkInfo> found;
  found.reserve(scanResult);

  LOG_DBG("WIFISEL", "Scan complete: %d results", scanResult);
  for (int i = 0; i < scanResult; i++) {
    char ssid[SSID_MAX_CHARS + 1];
    strlcpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid));
    const int32_t rssi = WiFi.RSSI(i);
    // Dump every beacon pre-filtering (ssid may be empty=hidden) — invaluable
    // for "network not found" reports, and free at LOG_LEVEL<2. The desktop
    // simulator's WiFi mock has no channel(int), so that field is stubbed there
    // (encryptionType(int) is available, so it stays real).
#ifdef SIMULATOR
    const int chan = -1;
#else
    const int chan = static_cast<int>(WiFi.channel(i));
#endif
    LOG_DBG("WIFISEL", "  [%d] ssid='%s' ch=%d rssi=%d auth=%d", i, ssid, chan, static_cast<int>(rssi),
            static_cast<int>(WiFi.encryptionType(i)));

    // Hidden networks have no usable list label; they are reachable through
    // the "Other network" row instead.
    if (ssid[0] == '\0') {
      continue;
    }

    const auto it =
        std::find_if(found.begin(), found.end(), [&ssid](const WifiNetworkInfo& n) { return n.ssid == ssid; });
    if (it == found.end()) {
      WifiNetworkInfo network;
      network.ssid = ssid;
      network.rssi = rssi;
      network.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
      found.push_back(std::move(network));
    } else if (rssi > it->rssi) {
      it->rssi = rssi;
      it->isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
  }

  // Sort: saved-password networks first, then by signal strength.
  std::sort(found.begin(), found.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
    if (a.hasSavedPassword != b.hasSavedPassword) return a.hasSavedPassword;
    return a.rssi > b.rssi;
  });

  WiFi.scanDelete();
  WifiScanCache::store(found);
  networks = std::move(found);

  // Restore the cursor by SSID; fall back to the top if the network vanished.
  selectedNetworkIndex = 0;
  if (!previousSelection.empty()) {
    const auto it = std::find_if(networks.begin(), networks.end(), [&previousSelection](const WifiNetworkInfo& n) {
      return n.ssid == previousSelection;
    });
    if (it != networks.end()) {
      selectedNetworkIndex = static_cast<int>(std::distance(networks.begin(), it));
    }
  }

  refreshingInBackground = false;
  state = WifiSelectionState::NETWORK_LIST;
  requestUpdate();
}

// ── Deferred radio work ─────────────────────────────────────────────────────

void WifiSelectionActivity::serviceRadioStep() {
  if (radioStep == RadioStep::None || static_cast<int32_t>(millis() - radioStepReadyAt) < 0) {
    return;
  }

  switch (radioStep) {
    case RadioStep::None:
      return;

    case RadioStep::ScanReset:
      WiFi.scanDelete();
      startWifiScanAsync();
      radioStep = RadioStep::None;
      return;

    case RadioStep::ConnectReset:
      WiFi.persistent(false);  // Suppress SDK NVS auto-connect
      WiFi.mode(WIFI_STA);
      WiFi.disconnect(true, true);  // Abort in-progress SDK auto-connect, clear NVS SSID
      radioStep = RadioStep::ConnectBegin;
      radioStepReadyAt = millis() + RADIO_MODE_SETTLE_MS;
      return;

    case RadioStep::ConnectBegin: {
      // Set DHCP hostname so the device is identifiable in the router's lease table.
      char dhcpHostname[40];
      NetworkNames::getDhcpHostname(dhcpHostname, sizeof(dhcpHostname));
      WiFi.setHostname(dhcpHostname);

      if (selectedRequiresPassword && !enteredPassword.empty()) {
        WiFi.begin(selectedSSID.c_str(), enteredPassword.c_str());
      } else {
        WiFi.begin(selectedSSID.c_str());
      }
      // The association clock starts here, not when the user pressed Connect,
      // so the reset sequence does not eat into the connect timeout.
      connectionStartTime = millis();
      connectFailureGraceUntilMs = connectionStartTime + CONNECT_FAILURE_GRACE_MS;
      radioStep = RadioStep::None;
      return;
    }
  }
}

// ── Selection ───────────────────────────────────────────────────────────────

const WifiNetworkInfo* WifiSelectionActivity::selectedNetwork() const {
  if (selectedNetworkIndex < 0 || selectedNetworkIndex >= static_cast<int>(networks.size())) {
    return nullptr;
  }
  return &networks[selectedNetworkIndex];
}

void WifiSelectionActivity::selectRow(const int index) {
  if (isManualRow(index)) {
    beginManualSsidEntry();
    return;
  }
  if (index < 0 || index >= static_cast<int>(networks.size())) {
    return;
  }

  const auto& network = networks[index];
  selectedSSID = network.ssid;
  selectedRequiresPassword = network.isEncrypted;
  usedSavedPassword = false;
  enteredPassword.clear();

  const auto* savedCred = WIFI_STORE.findCredential(selectedSSID);
  if (savedCred && !savedCred->password.empty()) {
    enteredPassword = savedCred->password;
    usedSavedPassword = true;
    LOG_DBG("WIFISEL", "Using saved password for %s", selectedSSID.c_str());
    state = WifiSelectionState::CONNECTING;
    attemptConnection();
    return;
  }

  if (selectedRequiresPassword) {
    beginPasswordEntry();
    return;
  }

  state = WifiSelectionState::CONNECTING;
  attemptConnection();
}

void WifiSelectionActivity::beginPasswordEntry() {
  state = WifiSelectionState::PASSWORD_ENTRY;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_WIFI_PASSWORD), "",
                                                                 PASSWORD_MAX_CHARS, InputType::Password),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                             requestUpdate();
                             return;
                           }
                           enteredPassword = std::get<KeyboardResult>(result.data).text;
                           state = WifiSelectionState::CONNECTING;
                           attemptConnection();
                         });
}

void WifiSelectionActivity::beginManualSsidEntry() {
  state = WifiSelectionState::MANUAL_SSID_ENTRY;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_WIFI_ENTER_SSID), "",
                                                                 SSID_MAX_CHARS, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = WifiSelectionState::NETWORK_LIST;
                             requestUpdate();
                             return;
                           }
                           selectedSSID = std::get<KeyboardResult>(result.data).text;
                           if (selectedSSID.empty()) {
                             state = WifiSelectionState::NETWORK_LIST;
                             requestUpdate();
                             return;
                           }
                           usedSavedPassword = false;
                           enteredPassword.clear();

                           const auto* savedCred = WIFI_STORE.findCredential(selectedSSID);
                           if (savedCred && !savedCred->password.empty()) {
                             enteredPassword = savedCred->password;
                             selectedRequiresPassword = true;
                             usedSavedPassword = true;
                             state = WifiSelectionState::CONNECTING;
                             attemptConnection();
                             return;
                           }
                           // A hidden network's encryption is unknown until we
                           // associate; ask for a password and treat an empty
                           // answer as an open network.
                           selectedRequiresPassword = true;
                           beginPasswordEntry();
                         });
}

// ── BLE provisioning ────────────────────────────────────────────────────────

void WifiSelectionActivity::startBleProvisioning() {
  if (!core::FeatureModules::hasCapability(core::Capability::BleWifiProvisioning)) {
    connectionError = tr(STR_BLE_DISABLED);
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }

  bleProvisioner.stop();
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  // The radio is going down, so any cached scan is no longer trustworthy.
  WifiScanCache::invalidate();
  // WiFi and BLE share the RF front end; give the radio time to power down
  // before advertising or bleProvisioner.start() fails. Blocking is acceptable
  // here — this is a one-shot, explicitly user-initiated mode switch.
  delay(80);

  if (!bleProvisioner.start("CrossPoint-WiFi")) {
    connectionError = tr(STR_ERROR_GENERAL_FAILURE);
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }

  state = WifiSelectionState::BLE_PROVISIONING;
  selectedSSID.clear();
  enteredPassword.clear();
  selectedRequiresPassword = false;
  usedSavedPassword = false;
  requestUpdate();
}

void WifiSelectionActivity::checkBleProvisioning() {
  if (!core::FeatureModules::hasCapability(core::Capability::BleWifiProvisioning)) {
    return;
  }

  std::string bleSsid;
  std::string blePassword;
  if (!bleProvisioner.takeCredentials(bleSsid, blePassword)) {
    return;
  }

  bleProvisioner.stop();

  selectedSSID = bleSsid;
  enteredPassword = blePassword;
  selectedRequiresPassword = !enteredPassword.empty();
  usedSavedPassword = false;

  LOG_DBG("WIFISEL", "Received BLE credentials for %s", selectedSSID.c_str());
  state = WifiSelectionState::CONNECTING;
  attemptConnection();
}

// ── Connecting ──────────────────────────────────────────────────────────────

void WifiSelectionActivity::attemptConnection() {
  // Connecting tears down any in-flight refresh scan (below), so the flag must
  // not survive — otherwise a later return to the list would poll a scan that
  // no longer exists and burn the retry budget on phantom failures.
  refreshingInBackground = false;
  connectionStartTime = millis();
  connectFailureGraceUntilMs = connectionStartTime + CONNECT_FAILURE_GRACE_MS;
  connectedIP.clear();
  connectionError.clear();
  requestUpdate();

  // Reset any previous STA session, then let the driver settle across two
  // loop ticks rather than blocking the main task for ~220 ms.
  WiFi.scanDelete();
  WiFi.disconnect(false);
  radioStep = RadioStep::ConnectReset;
  radioStepReadyAt = millis() + RADIO_RESET_SETTLE_MS;
}

void WifiSelectionActivity::abortConnection() {
  radioStep = RadioStep::None;
  WiFi.disconnect(false);
  connectionError.clear();
  connectedIP.clear();
}

void WifiSelectionActivity::checkConnectionStatus() {
  // Nothing to poll until WiFi.begin() has actually been issued.
  if (radioStep != RadioStep::None) {
    return;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = ipStr;
    onConnected();
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    if (static_cast<int32_t>(millis() - connectFailureGraceUntilMs) < 0) {
      return;
    }
    connectionError = (status == WL_NO_SSID_AVAIL) ? tr(STR_ERROR_NETWORK_NOT_FOUND) : tr(STR_ERROR_GENERAL_FAILURE);
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
    return;
  }

  if (millis() - connectionStartTime > CONNECTION_TIMEOUT_MS) {
    WiFi.disconnect();
    connectionError = tr(STR_ERROR_CONNECTION_TIMEOUT);
    state = WifiSelectionState::CONNECTION_FAILED;
    requestUpdate();
  }
}

void WifiSelectionActivity::onConnected() {
  const bool lastSsidChanged = WIFI_STORE.getLastConnectedSsid() != selectedSSID;
  WIFI_STORE.setLastConnectedSsid(selectedSSID);

  // A password the user just typed is saved without asking. Forgetting is one
  // button away in the list, so the extra confirmation step bought nothing.
  if (!usedSavedPassword && !enteredPassword.empty()) {
    // addCredential() persists the whole store, last-connected SSID included.
    if (!WIFI_STORE.addCredential(selectedSSID, enteredPassword)) {
      LOG_WRN("WIFISEL", "Failed to persist credential for %s", selectedSSID.c_str());
    }
    WifiScanCache::refreshSavedFlags();
  } else if (lastSsidChanged) {
    // Persist so the next boot's auto-connect targets this network. Guarded on
    // change: adopting an existing link runs on every network-backed activity
    // entry, and an unguarded write would burn SD erase cycles for nothing.
    WIFI_STORE.saveToFile();
  }

  // X3 only: sync the DS3231 RTC from NTP on the first successful connection.
  // The RTC drifts only ~2 ppm, so one sync suffices; users can force a
  // re-sync from Settings > Customise Status Bar > Sync Clock.
  if (halClock.isAvailable() && !SETTINGS.clockHasBeenSynced) {
    if (halClock.syncFromNTP()) {
      SETTINGS.clockHasBeenSynced = 1;
      if (!SETTINGS.saveToFile()) {
        LOG_ERR("WIFISEL", "Failed to persist clock sync flag");
      }
    }
  }

  onComplete(true);
}

// ── Loop ────────────────────────────────────────────────────────────────────

void WifiSelectionActivity::loop() {
  serviceRadioStep();

  switch (state) {
    case WifiSelectionState::RELEASING_BACKGROUND:
      if (BG_WIFI.isRunning()) {
        BG_WIFI.stop(true);  // keepWifi: hand the association to the foreground
        if (BG_WIFI.isRunning()) {
          // stop() declined to force-delete a task holding a mutex; a retry
          // would block another STOP_TIMEOUT_MS and fail identically.
          LOG_ERR("WIFISEL", "Background service did not release; continuing without handoff");
          backgroundReleaseFailed = true;
        }
      }
      evaluateEntry();
      return;

    case WifiSelectionState::PASSWORD_ENTRY:
    case WifiSelectionState::MANUAL_SSID_ENTRY:
      // A keyboard subactivity owns the screen; its result callback advances us.
      return;

    case WifiSelectionState::SCANNING:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        WiFi.scanDelete();
        radioStep = RadioStep::None;
        onComplete(false);
        return;
      }
      processWifiScanResults();
      return;

    case WifiSelectionState::AUTO_CONNECTING:
      // Escape hatch: without this the user waits out CONNECTION_TIMEOUT_MS
      // before they can pick a different network.
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        abortConnection();
        startWifiScan(false);
        return;
      }
      checkConnectionStatus();
      return;

    case WifiSelectionState::CONNECTING:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        abortConnection();
        if (networks.empty()) {
          startWifiScan(false);
        } else {
          state = WifiSelectionState::NETWORK_LIST;
          requestUpdate();
        }
        return;
      }
      checkConnectionStatus();
      return;

    case WifiSelectionState::BLE_PROVISIONING:
      checkBleProvisioning();
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        bleProvisioner.stop();
        startWifiScan(false);
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        startBleProvisioning();
      }
      return;

    case WifiSelectionState::CONNECTION_FAILED:
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        // Retry the same network rather than dumping the user back in the list.
        state = WifiSelectionState::CONNECTING;
        attemptConnection();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        if (networks.empty()) {
          startWifiScan(false);
        } else {
          state = WifiSelectionState::NETWORK_LIST;
          requestUpdate();
        }
      }
      return;

    case WifiSelectionState::FORGET_PROMPT:
      if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
          mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        if (forgetPromptSelection > 0) {
          forgetPromptSelection--;
          requestUpdate();
        }
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                 mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (forgetPromptSelection < 1) {
          forgetPromptSelection++;
          requestUpdate();
        }
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        if (forgetPromptSelection == 1) {
          WIFI_STORE.removeCredential(selectedSSID);
          const auto network = std::find_if(networks.begin(), networks.end(),
                                            [this](const WifiNetworkInfo& net) { return net.ssid == selectedSSID; });
          if (network != networks.end()) {
            network->hasSavedPassword = false;
          }
          WifiScanCache::refreshSavedFlags();
        }
        state = WifiSelectionState::NETWORK_LIST;
        requestUpdate();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        state = WifiSelectionState::NETWORK_LIST;
        requestUpdate();
      }
      return;

    case WifiSelectionState::NETWORK_LIST:
      break;
  }

  // ── Network list ──────────────────────────────────────────────────────────

  // A refresh scan runs while the (cached) list stays on screen and
  // interactive, so its results have to be collected from here — the SCANNING
  // branch above never runs in that mode.
  if (refreshingInBackground) {
    processWifiScanResults();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onComplete(false);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selectRow(selectedNetworkIndex);
    return;
  }

  // Right always rescans. Left is context-sensitive but only ever offers one
  // action, and the hint row names it — no more "Forget or BLE, depending".
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (!refreshingInBackground) {
      startWifiScan(false);
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    const auto* network = selectedNetwork();
    if (network != nullptr && network->hasSavedPassword) {
      selectedSSID = network->ssid;
      state = WifiSelectionState::FORGET_PROMPT;
      forgetPromptSelection = 0;  // Default to "Cancel"
      requestUpdate();
      return;
    }
    if (core::FeatureModules::hasCapability(core::Capability::BleWifiProvisioning)) {
      startBleProvisioning();
    }
    return;
  }

  buttonNavigator.onNext([this] {
    selectedNetworkIndex = ButtonNavigator::nextIndex(selectedNetworkIndex, rowCount());
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedNetworkIndex = ButtonNavigator::previousIndex(selectedNetworkIndex, rowCount());
    requestUpdate();
  });
}

// ── Rendering ───────────────────────────────────────────────────────────────

void WifiSelectionActivity::render(RenderLock&&) {
  // A keyboard subactivity owns the screen in these states.
  if (state == WifiSelectionState::PASSWORD_ENTRY || state == WifiSelectionState::MANUAL_SSID_ENTRY) {
    return;
  }

  renderer.clearScreen();

  switch (state) {
    case WifiSelectionState::RELEASING_BACKGROUND:
    case WifiSelectionState::AUTO_CONNECTING:
    case WifiSelectionState::SCANNING:
    case WifiSelectionState::CONNECTING:
      renderStatusScreen();
      break;
    case WifiSelectionState::NETWORK_LIST:
      renderNetworkList();
      break;
    case WifiSelectionState::BLE_PROVISIONING:
      renderBleProvisioning();
      break;
    case WifiSelectionState::CONNECTION_FAILED:
      renderConnectionFailed();
      break;
    case WifiSelectionState::FORGET_PROMPT:
      renderForgetPrompt();
      break;
    case WifiSelectionState::PASSWORD_ENTRY:
    case WifiSelectionState::MANUAL_SSID_ENTRY:
      break;
  }

  renderer.displayBuffer();
}

const char* WifiSelectionActivity::statusTitle() const {
  switch (state) {
    case WifiSelectionState::RELEASING_BACKGROUND:
      return tr(STR_WIFI_PREPARING);
    case WifiSelectionState::SCANNING:
      return tr(STR_SCANNING);
    case WifiSelectionState::AUTO_CONNECTING:
      return tr(STR_WIFI_RECONNECTING);
    default:
      return tr(STR_CONNECTING);
  }
}

std::string WifiSelectionActivity::statusDetail() const {
  if (state == WifiSelectionState::RELEASING_BACKGROUND) {
    return tr(STR_WIFI_RELEASING);
  }
  if (state == WifiSelectionState::SCANNING) {
    return tr(STR_WIFI_NETWORKS);
  }
  return selectedSSID;
}

void WifiSelectionActivity::renderStatusScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_NETWORKS));

  const int centre = pageHeight / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, centre - 30, statusTitle(), true, EpdFontFamily::BOLD);

  std::string detail = statusDetail();
  if (detail.length() > 36) {
    detail.replace(33, detail.length() - 33, "...");
  }
  if (!detail.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, centre + 5, detail.c_str());
  }

  // Elapsed seconds, so a slow association reads as progress rather than a hang.
  if (state == WifiSelectionState::CONNECTING || state == WifiSelectionState::AUTO_CONNECTING) {
    const unsigned long elapsed = (millis() - connectionStartTime) / 1000UL;
    char elapsedBuf[24];
    snprintf(elapsedBuf, sizeof(elapsedBuf), "%lus / %lus", elapsed, CONNECTION_TIMEOUT_MS / 1000UL);
    renderer.drawCenteredText(SMALL_FONT_ID, centre + 35, elapsedBuf);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderNetworkList() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const char* subtitle = refreshingInBackground ? tr(STR_WIFI_REFRESHING) : nullptr;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_NETWORKS),
                 subtitle);

  // Reserve the footer for the MAC address, which users need when their router
  // filters by hardware address.
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - footerHeight - metrics.verticalSpacing * 2;

  const int networkCount = static_cast<int>(networks.size());

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, rowCount(), selectedNetworkIndex,
      [this, networkCount](const int index) -> std::string {
        if (index >= networkCount) return tr(STR_WIFI_OTHER_NETWORK);
        return networks[index].ssid;
      },
      [this, networkCount](const int index) -> std::string {
        if (index >= networkCount) return tr(STR_WIFI_OTHER_NETWORK_DESC);
        const auto& net = networks[index];
        std::string sub = net.isEncrypted ? tr(STR_WIFI_SECURED) : tr(STR_WIFI_OPEN_NETWORK);
        if (net.hasSavedPassword) {
          sub += " · ";
          sub += tr(STR_WIFI_SAVED);
        }
        return sub;
      },
      [networkCount](const int index) { return index >= networkCount ? UIIcon::Hotspot : UIIcon::Wifi; },
      [this, networkCount](const int index) -> std::string {
        if (index >= networkCount) return "";
        return signalLabel(networks[index].rssi);
      });

  if (networks.empty() && !refreshingInBackground) {
    const int hintY = contentTop + contentHeight / 2;
    renderer.drawCenteredText(SMALL_FONT_ID, hintY, tr(STR_WIFI_NO_NETWORKS_HINT));
  }

  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding,
                    pageHeight - metrics.buttonHintsHeight - footerHeight + metrics.verticalSpacing / 2,
                    cachedMacAddress.c_str());

  const auto* network = selectedNetwork();
  const bool bleEnabled = core::FeatureModules::hasCapability(core::Capability::BleWifiProvisioning);
  const char* leftLabel = "";
  if (network != nullptr && network->hasSavedPassword) {
    leftLabel = tr(STR_FORGET_BUTTON);
  } else if (bleEnabled) {
    leftLabel = "BLE";
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONNECT), leftLabel,
                                            refreshingInBackground ? "" : tr(STR_WIFI_REFRESH));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderBleProvisioning() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_WIFI_SETUP));

  if (!core::FeatureModules::hasCapability(core::Capability::BleWifiProvisioning)) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_BLE_DISABLED), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  const int top = (pageHeight - 120) / 2;

  char advertisedAsBuf[48];
  snprintf(advertisedAsBuf, sizeof(advertisedAsBuf), "%s CrossPoint-WiFi", tr(STR_BLE_ADVERTISED_AS));

  renderer.drawCenteredText(UI_10_FONT_ID, top, advertisedAsBuf);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 20, tr(STR_BLE_WRITE_CREDENTIALS));
  renderer.drawCenteredText(UI_10_FONT_ID, top + 40, tr(STR_BLE_JSON_FORMAT_HINT));

  std::string status = bleProvisioner.getStatusMessage();
  if (status.length() > 35) {
    status.replace(32, status.length() - 32, "...");
  }
  renderer.drawCenteredText(SMALL_FONT_ID, top + 62, status.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderConnectionFailed() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_NETWORKS));

  const int centre = pageHeight / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, centre - 40, tr(STR_CONNECTION_FAILED), true, EpdFontFamily::BOLD);

  std::string ssidInfo = selectedSSID;
  if (ssidInfo.length() > 32) {
    ssidInfo.replace(29, ssidInfo.length() - 29, "...");
  }
  if (!ssidInfo.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, centre - 5, ssidInfo.c_str());
  }
  renderer.drawCenteredText(SMALL_FONT_ID, centre + 25, connectionError.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::renderForgetPrompt() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FORGET_NETWORK));

  const int centre = pageHeight / 2;

  std::string ssidInfo = selectedSSID;
  if (ssidInfo.length() > 32) {
    ssidInfo.replace(29, ssidInfo.length() - 29, "...");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, centre - 50, ssidInfo.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, centre - 20, tr(STR_FORGET_AND_REMOVE));

  constexpr int buttonWidth = 120;
  constexpr int buttonSpacing = 30;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;
  const int buttonY = centre + 20;

  if (forgetPromptSelection == 0) {
    const std::string text = "[" + std::string(tr(STR_CANCEL)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + 4, buttonY, tr(STR_CANCEL));
  }

  if (forgetPromptSelection == 1) {
    const std::string text = "[" + std::string(tr(STR_FORGET_BUTTON)) + "]";
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing, buttonY, text.c_str());
  } else {
    renderer.drawText(UI_10_FONT_ID, startX + buttonWidth + buttonSpacing + 4, buttonY, tr(STR_FORGET_BUTTON));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiSelectionActivity::onComplete(const bool connected) {
  ActivityResult result;
  result.isCancelled = !connected;
  if (connected) {
    result.data = WifiResult{true, selectedSSID, connectedIP};
  }
  setResult(std::move(result));
  finish();
}
