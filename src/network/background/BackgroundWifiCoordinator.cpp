#include "network/background/BackgroundWifiCoordinator.h"

#include <Arduino.h>
#include <Logging.h>

#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "core/features/FeatureModules.h"
#include "network/background/BackgroundServerPolicy.h"
#include "network/background/BackgroundWifiService.h"
#include "network/wifi/WifiUtil.h"
#include "util/WifiCredentialStore.h"

BackgroundWifiCoordinator BackgroundWifiCoordinator::instance;

background_server::AutoConnectInput BackgroundWifiCoordinator::buildAutoConnectInput() const {
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const WifiCredential* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);

  return background_server::AutoConnectInput{
      .alwaysModeEnabled = SETTINGS.keepsBackgroundServerOnWifiWhileAwake(),
      .waitingForNewCredential = APP_STATE.wifiAutoConnectWaitingForNewCredential,
      .skipCount = APP_STATE.wifiAutoConnectSkipCount,
      .lastConnectedSsid = lastSsid,
      .hasCredentialForLastSsid = cred != nullptr,
  };
}

bool BackgroundWifiCoordinator::attemptAutoConnect(const char* logTag) {
  const background_server::AutoConnectDecision decision =
      background_server::evaluateAutoConnect(buildAutoConnectInput());

  switch (decision.action) {
    case background_server::AutoConnectAction::None:
    case background_server::AutoConnectAction::SkipDueToBackoff:
    case background_server::AutoConnectAction::NoLastSsid:
      return false;
    case background_server::AutoConnectAction::BlockedWaitingForCredential:
      LOG_DBG(logTag, "WiFi auto-connect disabled until a new credential is added");
      return false;
    case background_server::AutoConnectAction::MissingCredentialForLastSsid: {
      APP_STATE.wifiAutoConnectWaitingForNewCredential = true;
      APP_STATE.wifiAutoConnectSkipCount = 0;
      APP_STATE.wifiAutoConnectBackoffLevel = 0;
      if (!APP_STATE.saveToFile()) {
        LOG_WRN(logTag, "Failed to persist WiFi credential recovery state");
      }
      LOG_DBG(logTag,
              "Saved WiFi credentials missing for last SSID; auto-connect disabled until a new credential is added");
      return false;
    }
    case background_server::AutoConnectAction::StartWithLastCredential: {
      const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
      const WifiCredential* cred = WIFI_STORE.findCredential(lastSsid);
      if (cred == nullptr) {
        return false;
      }
      LOG_DBG(logTag, "Starting background WiFi auto-connect to: %s", lastSsid.c_str());
      return BG_WIFI.start(cred->ssid.c_str(), cred->password.c_str());
    }
  }

  return false;
}

void BackgroundWifiCoordinator::applyReconcileDecision(const background_server::ReconcileDecision& decision) {
  switch (decision.action) {
    case background_server::ReconcileAction::None:
      return;
    case background_server::ReconcileAction::StopBgWifi:
      BG_WIFI.stop(decision.stopKeepWifi);
      return;
    case background_server::ReconcileAction::StartUsingCurrentConnection:
      BG_WIFI.startUsingCurrentConnection();
      return;
    case background_server::ReconcileAction::AttemptAutoConnect:
      if (attemptAutoConnect("BGCOORD")) {
        wifiAutoConnectAttempted_ = true;
      }
      return;
  }
}

void BackgroundWifiCoordinator::logAlwaysModeStateTransition() {
  const bool alwaysEnabled = core::FeatureModules::hasCapability(core::Capability::BackgroundServer) &&
                             SETTINGS.keepsBackgroundServerOnWifiWhileAwake();

  AlwaysBgServerState currentState = AlwaysBgServerState::Stopped;

  if (alwaysEnabled) {
    if (BG_WIFI.isRunning()) {
      currentState = AlwaysBgServerState::Running;
    } else if (BG_WIFI.isPendingOrRunning()) {
      currentState = AlwaysBgServerState::Backoff;
    } else {
      const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
      const WifiCredential* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);
      if (cred == nullptr) {
        currentState = AlwaysBgServerState::IdleNoCredential;
      }
    }
  }

  if (currentState != lastAlwaysModeState_) {
    if (currentState == AlwaysBgServerState::IdleNoCredential) {
      LOG_WRN("BGCOORD", "bg server idle: no saved credential");
    }
    lastAlwaysModeState_ = currentState;
  }
}

void BackgroundWifiCoordinator::reconcile(const BackgroundWifiReconcileContext& ctx) {
  logAlwaysModeStateTransition();

  const background_server::ReconcileDecision decision =
      background_server::evaluateReconcile(background_server::ReconcileInput{
          .backgroundWifiEnabled = core::FeatureModules::hasCapability(core::Capability::BackgroundServer) &&
                                   SETTINGS.keepsBackgroundServerOnWifiWhileAwake(),
          .blockedByActivity = ctx.blockedByActivity,
          .usbBackgroundServerRunning = ctx.usbBackgroundServerRunning,
          .staConnected = hasStaWifiConnection(),
          .bgWifiRunning = BG_WIFI.isRunning(),
          .bgWifiPendingOrRunning = BG_WIFI.isPendingOrRunning(),
          .wifiAutoConnectAttempted = wifiAutoConnectAttempted_,
      });

  applyReconcileDecision(decision);
}

void BackgroundWifiCoordinator::attemptBootAutoConnect() {
  if (!SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
    return;
  }

  if (APP_STATE.wifiAutoConnectSkipCount > 0) {
    APP_STATE.wifiAutoConnectSkipCount--;
    if (!APP_STATE.saveToFile()) {
      LOG_WRN("BGCOORD", "Failed to persist WiFi auto-connect backoff state");
    }
    LOG_DBG("BGCOORD", "WiFi auto-connect skipped (backoff remaining: %d)", APP_STATE.wifiAutoConnectSkipCount);
    return;
  }

  if (attemptAutoConnect("BGCOORD")) {
    wifiAutoConnectAttempted_ = true;
  }
}

bool BackgroundWifiCoordinator::beginTimedSleepAutoConnect(const char* logTag) { return attemptAutoConnect(logTag); }

bool BackgroundWifiCoordinator::waitForStaConnection(const uint32_t timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;
  while (!hasStaWifiConnection() && millis() < deadline) {
    delay(50);
  }
  return hasStaWifiConnection();
}

void BackgroundWifiCoordinator::endTimedSleepWifi() {
  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }
}

void BackgroundWifiCoordinator::onPrepareDeepSleep() {
  if (wifiAutoConnectAttempted_) {
    const bool hadActivity = BG_WIFI.hadApiActivity();
    if (BG_WIFI.isRunning()) {
      BG_WIFI.stop();
    }
    if (hadActivity || SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
      APP_STATE.wifiAutoConnectBackoffLevel = 0;
      APP_STATE.wifiAutoConnectSkipCount = 0;
      LOG_DBG("BGCOORD", "WiFi auto-connect backoff reset (hadActivity=%d, always=%d)", hadActivity ? 1 : 0,
              SETTINGS.keepsBackgroundServerOnWifiWhileAwake() ? 1 : 0);
    } else {
      if (APP_STATE.wifiAutoConnectBackoffLevel < 4) {
        APP_STATE.wifiAutoConnectBackoffLevel++;
      }
      APP_STATE.wifiAutoConnectSkipCount = (1U << APP_STATE.wifiAutoConnectBackoffLevel) - 1U;
      LOG_DBG("BGCOORD", "WiFi no API activity — backoff level %d, next skip: %d",
              APP_STATE.wifiAutoConnectBackoffLevel, APP_STATE.wifiAutoConnectSkipCount);
    }
  } else if (BG_WIFI.isRunning()) {
    BG_WIFI.stop();
  }
}
