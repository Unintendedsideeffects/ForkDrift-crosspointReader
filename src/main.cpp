#include <Arduino.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>

#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "UsbSerialProtocol.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "core/CoreBootstrap.h"
#include "core/features/FeatureLifecycle.h"
#include "core/features/FeatureModules.h"
#include "core/fonts/BuiltinFontRegistry.h"
#include "fontIds.h"
#include "network/BackgroundWebServer.h"
#include "network/BackgroundWifiService.h"
#include "util/AgentDebugLog.h"
#include "util/ButtonNavigator.h"
#include "util/FactoryResetUtils.h"
#include "util/FirmwareUpdateUtil.h"
#include "util/RecentBooksStore.h"
#include "util/ScreenshotUtil.h"
#include "util/UsbMscPrompt.h"
#include "util/WifiCredentialStore.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
BackgroundWebServer& backgroundServer = BackgroundWebServer::getInstance();
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap());

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

namespace {
constexpr char kCrossPointDataDir[] = "/.crosspoint";
constexpr char kFactoryResetMarkerFile[] = "/.factory-reset-pending";
constexpr char kUsbMscSessionMarkerFile[] = "/.crosspoint/usb-msc-active";
constexpr uint32_t kSafeModeSleepHoldMs = 1500;

enum class UsbMscSessionState { Idle, Prompt, Active };

UsbMscSessionState usbMscSessionState = UsbMscSessionState::Idle;
bool usbConnectedLast = false;
UsbSerialProtocol usbSerialProtocol;
bool usbMscScreenNeedsRedraw = false;
bool usbMscRemountPending = false;
bool safeModeActive = false;
bool activityManagerReady = false;
bool displayAndFontsReady = false;

void renderSafeModeScreen(const char* message) {
  if (!renderer.getFrameBuffer()) {
    return;
  }

  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int bodyLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int bodyMaxWidth = renderer.getScreenWidth() - 48;
  const auto detailLines = renderer.wrappedText(UI_10_FONT_ID, message, bodyMaxWidth, 4);
  const int detailHeight = static_cast<int>(detailLines.size()) * (bodyLineHeight + 4);

  int y = (renderer.getScreenHeight() - (titleLineHeight + 16 + detailHeight + bodyLineHeight + 12)) / 2;
  if (y < 40) {
    y = 40;
  }

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, y, "Safe Mode", true, EpdFontFamily::BOLD);
  y += titleLineHeight + 16;

  for (const auto& line : detailLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true);
    y += bodyLineHeight + 4;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, y + 12, "Hold power to sleep", true);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void enterSafeMode(const char* message) {
  safeModeActive = true;
  LOG_ERR("SAFE", "Entering safe mode: %s", message);
  if (activityManagerReady) {
    activityManager.goToFullScreenMessage(std::string("Safe mode: ") + message, EpdFontFamily::BOLD);
    activityManager.loop();
    return;
  }
  renderSafeModeScreen(message);
}

void renderUsbMscPrompt() {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 260, "Connect as Mass Storage?", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 300, "SD card will be unavailable on-device", true);
  const auto labels = mappedInputManager.mapLabels(tr(STR_NO), tr(STR_YES), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void renderUsbMscLockedScreen() {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 260, "Mass Storage Active", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 300, "Disconnect USB cable to return", true);
  renderer.displayBuffer();
}

void enterUsbMscSession() {
  LOG_INF("USBMSC", "Entering USB mass storage lock mode");
  APP_STATE.saveToFile();
  if (!SETTINGS.saveToFile()) {
    LOG_WRN("USBMSC", "Failed to persist settings before USB MSC session");
  }

  activityManager.goHome();
  activityManager.loop();

  Storage.mkdir(kCrossPointDataDir);
  Storage.writeFile(kUsbMscSessionMarkerFile, "1");
  usbSerialProtocol.reset();
  usbMscSessionState = UsbMscSessionState::Active;
  usbMscScreenNeedsRedraw = true;
}

void exitUsbMscSession() {
  LOG_INF("USBMSC", "Exiting USB mass storage lock mode");
  usbMscSessionState = UsbMscSessionState::Idle;
  usbMscScreenNeedsRedraw = false;
  usbMscRemountPending = true;
}

void applyPendingFactoryReset() {
  if (!Storage.exists(kFactoryResetMarkerFile)) {
    return;
  }

  LOG_INF("RESET", "Pending factory reset marker detected");

  if (!FactoryResetUtils::resetCrossPointMetadataPreservingContent()) {
    LOG_ERR("RESET", "Failed to reset CrossPoint metadata/cache. Retrying on next boot.");
    return;
  }

  if (!Storage.remove(kFactoryResetMarkerFile)) {
    LOG_ERR("RESET", "Metadata reset completed, but marker removal failed: %s", kFactoryResetMarkerFile);
    return;
  }

  LOG_INF("RESET", "Factory reset completed from pending marker (cache cleared, user files preserved)");
}
}  // namespace

// True if BG_WIFI.start() was called this wake — used by enterDeepSleep() to
// decide whether to update the WiFi auto-connect backoff counters.
static bool wifiAutoConnectAttempted = false;

bool hasStaWifiConnection() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  if ((wifiMode & WIFI_MODE_STA) == 0) {
    return false;
  }
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void reconcileBackgroundWifiServer() {
  const bool backgroundWifiEnabled = core::FeatureModules::hasCapability(core::Capability::BackgroundServer) &&
                                     SETTINGS.keepsBackgroundServerOnWifiWhileAwake();
  const bool blockedByActivity = activityManager.blocksBackgroundServer();
  const bool staConnected = hasStaWifiConnection();
  const bool autoConnectInFlight = BG_WIFI.isRunning() && wifiAutoConnectAttempted && !staConnected;

  // #region agent log
  static unsigned long lastAgentReconcileLogMs = 0;
  if (millis() - lastAgentReconcileLogMs >= 3000) {
    lastAgentReconcileLogMs = millis();
    char data[240];
    snprintf(data, sizeof(data),
             "{\"enabled\":%s,\"blockedByActivity\":%s,\"staConnected\":%s,\"bgWebRunning\":%s,"
             "\"bgWifiRunning\":%s,\"autoConnectInFlight\":%s,\"wifiStatus\":%d}",
             backgroundWifiEnabled ? "true" : "false", blockedByActivity ? "true" : "false",
             staConnected ? "true" : "false", backgroundServer.isRunning() ? "true" : "false",
             BG_WIFI.isRunning() ? "true" : "false", autoConnectInFlight ? "true" : "false",
             static_cast<int>(WiFi.status()));
    agentDebugLog("initial", "H1,H2,H3", "main.cpp:reconcileBackgroundWifiServer", "background wifi reconcile state",
                  data);
  }
  // #endregion

  if (backgroundServer.isRunning()) {
    return;
  }

  if (!backgroundWifiEnabled || blockedByActivity) {
    if (BG_WIFI.isRunning()) {
      BG_WIFI.stop(blockedByActivity);
    }
    return;
  }

  if (staConnected) {
    if (!BG_WIFI.isRunning()) {
      LOG_DBG("MAIN", "WiFi connected; starting background file server");
      BG_WIFI.startUsingCurrentConnection();
    }
    return;
  }

  if (!autoConnectInFlight && BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }
}

void refreshGlobalStatusBarOnWifiChange() {
  static bool lastStaConnected = hasStaWifiConnection();

  const bool staConnected = hasStaWifiConnection();
  if (staConnected == lastStaConnected) {
    return;
  }

  lastStaConnected = staConnected;
  if (SETTINGS.globalStatusBar) {
    activityManager.requestUpdate();
  }
}

void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    return;
  }

  const auto start = millis();
  bool abort = false;
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

void enterDeepSleep() {
  HalPowerManager::Lock powerLock;
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  // Update WiFi auto-connect backoff before sleeping (only if we attempted this wake)
  if (wifiAutoConnectAttempted) {
    const bool hadActivity = BG_WIFI.hadApiActivity();
    if (BG_WIFI.isRunning()) {
      BG_WIFI.stop();
    }
    if (hadActivity || SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
      // Successful sync, OR user explicitly chose Always mode: reset backoff
      // so the server is available on the very next wake. Backing off in
      // Always mode is the "not always on" bug — the user opted in to the
      // battery cost; quiet wakes must not silently disable auto-connect.
      APP_STATE.wifiAutoConnectBackoffLevel = 0;
      APP_STATE.wifiAutoConnectSkipCount = 0;
      LOG_DBG("MAIN", "WiFi auto-connect backoff reset (hadActivity=%d, always=%d)", hadActivity ? 1 : 0,
              SETTINGS.keepsBackgroundServerOnWifiWhileAwake() ? 1 : 0);
    } else {
      // No push/pull received: increase backoff exponentially (cap at level 4 = 15 skips)
      if (APP_STATE.wifiAutoConnectBackoffLevel < 4) {
        APP_STATE.wifiAutoConnectBackoffLevel++;
      }
      APP_STATE.wifiAutoConnectSkipCount = (1U << APP_STATE.wifiAutoConnectBackoffLevel) - 1U;
      LOG_DBG("MAIN", "WiFi no API activity — backoff level %d, next skip: %d", APP_STATE.wifiAutoConnectBackoffLevel,
              APP_STATE.wifiAutoConnectSkipCount);
    }
  } else if (BG_WIFI.isRunning()) {
    BG_WIFI.stop();
  }

  APP_STATE.saveToFile();

  activityManager.goToSleep();

  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

bool setupDisplayAndFonts() {
  if (displayAndFontsReady) {
    return true;
  }

  display.begin();
  if (!renderer.begin()) {
    LOG_ERR("MAIN", "Renderer initialization failed");
    safeModeActive = true;
    return false;
  }

  core::BuiltinFontRegistry::registerUiFonts(renderer);
  LOG_DBG("MAIN", "Display initialized");

  if (!activityManager.begin()) {
    enterSafeMode("UI startup failed");
    return false;
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  if (!core::BuiltinFontRegistry::registerAllFonts(renderer)) {
    enterSafeMode("Font initialization failed");
    return false;
  }

  core::FeatureLifecycle::onFontSetup(renderer);
  displayAndFontsReady = true;
  LOG_DBG("MAIN", "Fonts setup");
  return true;
}

void setup() {
  t1 = millis();

  HalSystem::begin();
  gpio.begin();
  powerManager.begin();

  const bool usbConnectedAtBoot = gpio.isUsbConnected();
#ifdef ENABLE_SERIAL_LOG
  if (usbConnectedAtBoot) {
    Serial.begin(115200);
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
      delay(10);
    }
  }
#endif
  core::CoreBootstrap::initializeFeatureSystem(usbConnectedAtBoot);

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    if (setupDisplayAndFonts()) {
      enterSafeMode("SD card unavailable");
    }
    return;
  }

  HalSystem::checkPanic();

  core::FeatureLifecycle::onStorageReady();

  applyPendingFactoryReset();
  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    usbConnectedLast = gpio.isUsbConnected();
    if (Storage.exists(kUsbMscSessionMarkerFile)) {
      LOG_WRN("USBMSC", "Detected stale USB MSC marker; recovering SD ownership");
      Storage.remove(kUsbMscSessionMarkerFile);
    }
  }

  SETTINGS.loadFromFile();
  core::FeatureLifecycle::onSettingsLoaded(renderer);
  I18N.loadSettings();
  WIFI_STORE.loadFromFile();
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  const bool wokeFromSleep = (wakeupReason == HalGPIO::WakeupReason::PowerButton);
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                   SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // USB power connected: stay awake so user can access the file server
      LOG_DBG("MAIN", "Wakeup reason: After USB Power - staying awake for file transfer");
      break;
    case HalGPIO::WakeupReason::AfterFlash:
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  if (!setupDisplayAndFonts()) {
    return;
  }

  FirmwareUpdateUtil::handleLocalUpdateBootFlow(renderer, mappedInputManager);

  activityManager.goToBoot();

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    if (APP_STATE.lastSleepFromReader) {
      APP_STATE.pendingHomeFullRefresh = true;
    }
    activityManager.goHome();
  } else {
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath.clear();
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }

  // WiFi auto-connect on boot or wake from sleep (background, silent).
  // keepsBackgroundServerOnWifiWhileAwake() is true only for BACKGROUND_SERVER_ALWAYS,
  // so the wokeFromSleep guard is intentionally omitted here — "always on" means every boot.
  if (SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
    if (APP_STATE.wifiAutoConnectWaitingForNewCredential) {
      LOG_DBG("MAIN", "WiFi auto-connect disabled until a new credential is added");
    } else if (APP_STATE.wifiAutoConnectSkipCount > 0) {
      // Still in backoff — consume one skip cycle
      APP_STATE.wifiAutoConnectSkipCount--;
      APP_STATE.saveToFile();
      LOG_DBG("MAIN", "WiFi auto-connect skipped (backoff remaining: %d)", APP_STATE.wifiAutoConnectSkipCount);
    } else {
      // Attempt silent background connect using last known credentials
      const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
      if (!lastSsid.empty()) {
        const auto* cred = WIFI_STORE.findCredential(lastSsid);
        if (cred) {
          LOG_DBG("MAIN", "Starting background WiFi auto-connect to: %s", lastSsid.c_str());
          BG_WIFI.start(cred->ssid.c_str(), cred->password.c_str());
          wifiAutoConnectAttempted = true;
        } else {
          APP_STATE.wifiAutoConnectWaitingForNewCredential = true;
          APP_STATE.wifiAutoConnectSkipCount = 0;
          APP_STATE.wifiAutoConnectBackoffLevel = 0;
          APP_STATE.saveToFile();
          LOG_DBG(
              "MAIN",
              "Saved WiFi credentials missing for last SSID; auto-connect disabled until a new credential is added");
        }
      }
    }
  }

  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;
  static unsigned long lastActivityTime = millis();
  static bool screenshotButtonsReleased = true;

  if (safeModeActive) {
    gpio.update();
    if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > kSafeModeSleepHoldMs) {
      if (renderer.getFrameBuffer()) {
        display.deepSleep();
      }
      powerManager.startDeepSleep(gpio);
    }
    delay(50);
    return;
  }

  gpio.update();
  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  if (core::FeatureModules::hasCapability(core::Capability::UsbMassStorage)) {
    const bool usbConnected = gpio.isUsbConnected();
    const bool hostSupportsUsbSerial = static_cast<bool>(logSerial);

    if (usbMscRemountPending) {
      usbMscRemountPending = false;
      Storage.remove(kUsbMscSessionMarkerFile);
      if (!Storage.begin()) {
        LOG_ERR("USBMSC", "SD remount failed after USB MSC exit");
        enterSafeMode("SD card remount failed");
        usbConnectedLast = usbConnected;
        return;
      }
      activityManager.goHome();
      usbConnectedLast = usbConnected;
      return;
    }

    if (UsbMscPrompt::shouldShowOnUsbConnect(SETTINGS.usbMscPromptOnConnect != 0, usbConnected, usbConnectedLast,
                                             hostSupportsUsbSerial, usbMscSessionState == UsbMscSessionState::Idle)) {
      usbMscSessionState = UsbMscSessionState::Prompt;
      usbMscScreenNeedsRedraw = true;
    }

    if (usbMscSessionState == UsbMscSessionState::Prompt) {
      backgroundServer.loop(usbConnected, false);
      if (!usbConnected) {
        usbMscSessionState = UsbMscSessionState::Idle;
        activityManager.requestUpdate(true);
      } else {
        if (usbMscScreenNeedsRedraw) {
          renderUsbMscPrompt();
          usbMscScreenNeedsRedraw = false;
        }
        if (mappedInputManager.wasReleased(MappedInputManager::Button::Confirm)) {
          enterUsbMscSession();
        } else if (mappedInputManager.wasReleased(MappedInputManager::Button::Back)) {
          usbMscSessionState = UsbMscSessionState::Idle;
          activityManager.requestUpdate(true);
        }
      }
      usbConnectedLast = usbConnected;
      delay(20);
      return;
    }

    if (usbMscSessionState == UsbMscSessionState::Active) {
      usbSerialProtocol.loop();
      if (!usbConnected) {
        exitUsbMscSession();
      } else if (usbMscScreenNeedsRedraw) {
        renderUsbMscLockedScreen();
        usbMscScreenNeedsRedraw = false;
      }
      usbConnectedLast = usbConnected;
      delay(20);
      return;
    }

    usbConnectedLast = usbConnected;
  }

  {
    const bool usbConn = gpio.isUsbConnected();
    const bool suppressUsbBackgroundServer = BG_WIFI.isRunning();
    const bool allowRun = core::FeatureModules::hasCapability(core::Capability::BackgroundServer) &&
                          SETTINGS.backgroundServerOnCharge && usbConn && !activityManager.blocksBackgroundServer() &&
                          !suppressUsbBackgroundServer;
    backgroundServer.loop(usbConn, allowRun);
  }

  reconcileBackgroundWifiServer();
  refreshGlobalStatusBarOnWifiChange();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep() ||
      backgroundServer.shouldPreventAutoSleep()) {
    lastActivityTime = millis();
    powerManager.setPowerSaving(false);
  }

  const bool screenshotChordPressed = gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN);
  if (screenshotChordPressed) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      APP_STATE.pendingScreenshot = true;
    }
  } else {
    screenshotButtonsReleased = true;
  }

  if (APP_STATE.pendingScreenshot) {
    APP_STATE.pendingScreenshot = false;
    RenderLock lock;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (screenshotChordPressed) {
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    return;
  }

  // Remote open-book: USB or HTTP set pendingOpenPath; drain it here on the main loop.
  if (!APP_STATE.pendingOpenPath.empty()) {
    std::string path = std::move(APP_STATE.pendingOpenPath);
    activityManager.goToReader(std::move(path));
    return;
  }

  // Remote page turn: translate cross-task volatile signal into a virtual button injection.
  const int8_t pageTurn = APP_STATE.pendingPageTurn;
  if (pageTurn != 0) {
    APP_STATE.pendingPageTurn = 0;
    mappedInputManager.injectVirtualActivation(pageTurn > 0 ? MappedInputManager::Button::PageForward
                                                            : MappedInputManager::Button::PageBack);
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    RenderLock lock;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

#if LOG_LEVEL >= 2
  const unsigned long activityStartTime = millis();
#endif
  activityManager.loop();
#if LOG_LEVEL >= 2
  const unsigned long activityDuration = millis() - activityStartTime;
#endif

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
#if LOG_LEVEL >= 2
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
#endif
  }

  if (activityManager.skipLoopDelay() || backgroundServer.wantsFastLoop()) {
    powerManager.setPowerSaving(false);
    yield();
  } else if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
    powerManager.setPowerSaving(true);
    delay(50);
  } else {
    delay(10);
  }
}
