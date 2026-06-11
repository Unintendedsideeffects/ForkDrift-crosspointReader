#include <Arduino.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "BuiltinFonts.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FeatureFlags.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "SdCardFontSystem.h"
#include "UsbSerialProtocol.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/settings/RecoveryMenuActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "core/CoreBootstrap.h"
#include "core/features/FeatureLifecycle.h"
#include "core/features/FeatureModules.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"
#include "network/BackgroundServerPolicy.h"
#include "network/BackgroundWebServer.h"
#include "network/BackgroundWifiService.h"
#include "util/ButtonNavigator.h"
#include "util/FactoryResetUtils.h"
#include "util/FirmwareUpdateUtil.h"
#include "util/RecentBooksStore.h"
#include "util/ScreenshotUtil.h"
#if ENABLE_WIFI_CLOCK
#include "util/TimeSync.h"
#endif
#include "util/UsbMscPrompt.h"
#ifdef SIMULATOR
#include "simulator/SimulatorSmokeTest.h"
#endif
#include "util/WifiCredentialStore.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
BackgroundWebServer& backgroundServer = BackgroundWebServer::getInstance();
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// SILENT RESTART FEATURE - This is a restart without showing the boot splash screen
void silentRestart() {
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  delay(50);
  ESP.restart();
}

static bool backgroundServerKeepsWifiWhileAwake() {
  if (SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
    return true;
  }
  return core::FeatureModules::hasCapability(core::Capability::BackgroundServer) && SETTINGS.backgroundServerOnCharge &&
         gpio.isUsbConnected();
}

void recoverHeapAfterWifi(const char* tag) {
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    return;
  }

  // When a background-server mode wants WiFi kept up while awake (Always mode, or
  // On-Charge while plugged in), reboot to the boot-time auto-connect path if a
  // foreground activity tears WiFi down while the server should stay available.
  if (backgroundServerKeepsWifiWhileAwake()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
    return;
  }

  // Otherwise WiFi is meant to go idle. Fully de-init it (returns the WiFi/LWIP
  // heap), and reboot only when the largest contiguous block is still too small
  // for later memory-heavy work — the ESP32 heap cannot be compacted in place.
  constexpr uint32_t kMinLargestBlockToSkipReboot = 90000;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(30);

  const uint32_t largestBlock = ESP.getMaxAllocHeap();
  LOG_DBG(tag, "Heap after WiFi teardown: free=%u largest=%u", ESP.getFreeHeap(), largestBlock);

  if (largestBlock < kMinLargestBlockToSkipReboot) {
    LOG_DBG(tag, "Heap too fragmented (largest=%u), silent restart to recover", largestBlock);
    silentRestart();
  }
}

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
  // TODO: magic numbers here are boun
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
  if (!APP_STATE.saveToFile()) {
    LOG_WRN("USBMSC", "Failed to persist app state before USB MSC session");
  }
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

static background_server::AutoConnectInput buildBackgroundWifiAutoConnectInput() {
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

static bool attemptBackgroundWifiAutoConnect(const char* logTag) {
  const background_server::AutoConnectDecision decision =
      background_server::evaluateAutoConnect(buildBackgroundWifiAutoConnectInput());

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

void reconcileBackgroundWifiServer() {
  const background_server::ReconcileDecision decision =
      background_server::evaluateReconcile(background_server::ReconcileInput{
          .backgroundWifiEnabled = core::FeatureModules::hasCapability(core::Capability::BackgroundServer) &&
                                   SETTINGS.keepsBackgroundServerOnWifiWhileAwake(),
          .blockedByActivity = activityManager.blocksBackgroundServer(),
          .usbBackgroundServerRunning = backgroundServer.isRunning(),
          .staConnected = hasStaWifiConnection(),
          .bgWifiRunning = BG_WIFI.isRunning(),
          .bgWifiPendingOrRunning = BG_WIFI.isPendingOrRunning(),
          .wifiAutoConnectAttempted = wifiAutoConnectAttempted,
      });

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
      if (attemptBackgroundWifiAutoConnect("MAIN")) {
        wifiAutoConnectAttempted = true;
      }
      return;
  }
}

void refreshGlobalStatusBarOnWifiChange() {
  static bool lastStaConnected = hasStaWifiConnection();

  const bool staConnected = hasStaWifiConnection();
  if (staConnected == lastStaConnected) {
    return;
  }

  lastStaConnected = staConnected;
  activityManager.requestUpdate();
}

#if ENABLE_WIFI_CLOCK
void refreshClockOnTick() {
  static unsigned long lastClockRefreshMs = 0;
  constexpr unsigned long kClockRefreshIntervalMs = 15UL * 60UL * 1000UL;

  if (!hasStaWifiConnection()) {
    return;
  }

  const unsigned long nowMs = millis();
  if (lastClockRefreshMs != 0 && nowMs - lastClockRefreshMs < kClockRefreshIntervalMs) {
    return;
  }

  lastClockRefreshMs = nowMs;
  activityManager.requestUpdate();
}
#endif

void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    return;
  }

  const auto start = millis();
  bool abort = false;
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonWakeDuration()) ? SETTINGS.getPowerButtonWakeDuration() - calibration : 1;

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

  if (!APP_STATE.saveToFile()) {
    LOG_WRN("MAIN", "Failed to persist app state before deep sleep");
  }

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
  renderer.begin();

  LOG_DBG("MAIN", "Display initialized");

  if (!activityManager.begin()) {
    enterSafeMode("UI startup failed");
    return false;
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  registerBuiltinFonts(renderer);

  core::FeatureLifecycle::onFontSetup(renderer);
  displayAndFontsReady = true;
  sdFontSystem.begin(renderer);
  LOG_DBG("MAIN", "Fonts setup");
  return true;
}

void setup() {
  t1 = millis();

  HalSystem::begin();
#ifndef SIMULATOR
  HalSystem::setSettingsProvider([]() { return SETTINGS.getCondensedSettings(); });
#endif
  gpio.begin();
  powerManager.begin();
  // Probe the DS3231 RTC (X3 only; no-op on X4). Must follow powerManager.begin(),
  // which initialises the shared I2C bus.
  halClock.begin();

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

  bool sdReady = Storage.begin();
#ifdef SIMULATOR
  // Let the smoke harness exercise the SD-missing Safe Mode path, which is
  // otherwise hardware-only (the simulator's HalStorage::begin() always succeeds).
  if (std::getenv("FORKDRIFT_SIMULATOR_SD_FAIL") != nullptr) {
    LOG_WRN("MAIN", "Simulator: forcing SD init failure (FORKDRIFT_SIMULATOR_SD_FAIL)");
    sdReady = false;
  }
#endif
  if (!sdReady) {
    LOG_ERR("MAIN", "SD card initialization failed");
    if (setupDisplayAndFonts()) {
      enterSafeMode("SD card unavailable");
    }
    return;
  }

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
  HalSystem::checkPanic();
  invalidateSleepImageCache();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
#if ENABLE_WIFI_CLOCK
  TimeSync::restorePersistedTime();
#endif
  core::FeatureLifecycle::onSettingsLoaded(renderer);
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
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonWakeDuration(),
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

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
#ifdef SIMULATOR
  // Let the smoke harness boot straight into the recovery menu without the
  // physical UP+POWER combo (which the simulator cannot inject at boot).
  if (std::getenv("FORKDRIFT_SIMULATOR_RECOVERY") != nullptr) {
    recoveryFirmwareMode = true;
    LOG_INF("MAIN", "Simulator: forcing recovery mode (FORKDRIFT_SIMULATOR_RECOVERY)");
  }
#endif
  // cppcheck-suppress knownConditionTrueFalse -- always false in simulator-only SIMULATOR path above
  if (!recoveryFirmwareMode && wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  if (!setupDisplayAndFonts()) {
    return;
  }

  FirmwareUpdateUtil::handleLocalUpdateBootFlow(renderer, mappedInputManager);

  // A heap-recovery reboot (recoverHeapAfterWifi) stamps this magic into RTC
  // memory before ESP.restart(). Consume it once so the restart skips the boot
  // splash and lands directly on its declared target — without this read side the
  // "silent" restart replayed the full BootActivity animation on every back-out.
  const bool silentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t silentRebootDestination = silentRebootTarget;
  silentRebootMagic = 0;

  if (!silentReboot) {
    activityManager.goToBoot();
  }

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: open the recovery menu (firmware flash,
    // clear cache, reset settings, factory reset) so a device made unusable by
    // bad settings or corrupt cache can be recovered without USB flashing.
    activityManager.replaceActivity(std::make_unique<RecoveryMenuActivity>(renderer, mappedInputManager));
  } else if (HalSystem::isRebootFromCrash()) {
    // If we rebooted from a panic/WDT/brownout, go to crash report screen to show the info
    activityManager.goToCrashReport();
  } else if (silentReboot) {
    if (silentRebootDestination == SILENT_REBOOT_TARGET_READER && !APP_STATE.openEpubPath.empty()) {
      const auto path = APP_STATE.openEpubPath;
      APP_STATE.openEpubPath.clear();
      APP_STATE.readerActivityLoadCount++;
      if (!APP_STATE.saveToFile()) {
        LOG_WRN("MAIN", "Failed to persist reader resume state");
      }
      activityManager.goToReader(path);
    } else {
      activityManager.goHome();
    }
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    if (APP_STATE.lastSleepFromReader && !APP_STATE.transparentSleepRestoredOnWake) {
      APP_STATE.pendingHomeFullRefresh = true;
    }
    activityManager.goHome();
  } else {
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath.clear();
    APP_STATE.readerActivityLoadCount++;
    if (!APP_STATE.saveToFile()) {
      LOG_WRN("MAIN", "Failed to persist reader resume state");
    }
    activityManager.goToReader(path);
  }

  // WiFi auto-connect on boot or wake from sleep (background, silent).
  // keepsBackgroundServerOnWifiWhileAwake() is true only for BACKGROUND_SERVER_ALWAYS,
  // so the wokeFromSleep guard is intentionally omitted here — "always on" means every boot.
  if (SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
    if (APP_STATE.wifiAutoConnectSkipCount > 0) {
      APP_STATE.wifiAutoConnectSkipCount--;
      if (!APP_STATE.saveToFile()) {
        LOG_WRN("MAIN", "Failed to persist WiFi auto-connect backoff state");
      }
      LOG_DBG("MAIN", "WiFi auto-connect skipped (backoff remaining: %d)", APP_STATE.wifiAutoConnectSkipCount);
    } else if (attemptBackgroundWifiAutoConnect("MAIN")) {
      wifiAutoConnectAttempted = true;
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
  static bool screenshotComboActive = false;

  if (safeModeActive) {
#ifdef SIMULATOR
    // The smoke tick below the safe-mode early-return never runs, so verify the
    // Safe Mode landing here instead (passes only when SD failure was requested).
    runSimulatorSmokeTestSafeModeTick();
#endif
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
      invalidateSleepImageCache();
      activityManager.goHome();
      usbConnectedLast = usbConnected;
      return;
    }
    // TODO: this actually doesn't do much, limitation of the hardware
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
    const bool allowRun = background_server::shouldRunOnChargeBackgroundServer(background_server::OnChargeServerInput{
        .hasBackgroundServerCapability = core::FeatureModules::hasCapability(core::Capability::BackgroundServer),
        .backgroundServerOnCharge = SETTINGS.backgroundServerOnCharge != 0,
        .usbConnected = usbConn,
        .blockedByActivity = activityManager.blocksBackgroundServer(),
        .bgWifiRunning = suppressUsbBackgroundServer,
    });
    static bool bgServerWasRunning = false;
    backgroundServer.loop(usbConn, allowRun);
    const bool bgServerIsRunning = backgroundServer.isRunning();
    if (bgServerIsRunning && !bgServerWasRunning) {
      core::FeatureLifecycle::onBackgroundServerStarted();
    }
    bgServerWasRunning = bgServerIsRunning;
  }

  reconcileBackgroundWifiServer();
#if ENABLE_WIFI_CLOCK
  TimeSync::loop(hasStaWifiConnection());
#endif
  refreshGlobalStatusBarOnWifiChange();
#if ENABLE_WIFI_CLOCK
  refreshClockOnTick();
#endif

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep() ||
      features::status_overlay::preventsAutoSleep() ||  // cppcheck-suppress knownConditionTrueFalse
      backgroundServer.shouldPreventAutoSleep()) {
    lastActivityTime = millis();
    powerManager.setPowerSaving(false);
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      APP_STATE.pendingScreenshot = true;
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  if (APP_STATE.pendingScreenshot) {
    APP_STATE.pendingScreenshot = false;
    RenderLock lock;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonWakeDuration()) {
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    return;
  }

  // Remote open-book: USB or HTTP set pendingOpenPath; drain it here on the main loop.
  if (std::string path = APP_STATE.takePendingOpenPath(); !path.empty()) {
    activityManager.goToReader(std::move(path));
    return;
  }

  // Remote page turn: translate cross-task volatile signal into a virtual button injection.
  // Also reset lastActivityTime here so a remote page turn prevents sleep even though
  // the sleep check runs before injection in the same frame.
  const int8_t pageTurn = APP_STATE.takePendingPageTurn();
  if (pageTurn != 0) {
    lastActivityTime = millis();
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
  // Clear unconsumed virtual bits — activities that don't call wasPressed(idx) would otherwise
  // leave bits set forever, causing wasAnyPressed() to permanently block auto-sleep.
#ifndef SIMULATOR
  gpio.drainVirtualMask();
#endif
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

#ifdef SIMULATOR
  runSimulatorSmokeTestTick();
#endif

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
