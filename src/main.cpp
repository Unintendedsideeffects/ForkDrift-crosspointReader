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
#ifndef SIMULATOR
#include <esp_ota_ops.h>
#endif

#include <algorithm>
#include <cinttypes>  // PRIu64 for deep-sleep timer logging (transitively present on ESP32, not on host)
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
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "core/OrientationManager.h"
#if ENABLE_TERMINUS_SLEEP
#include "features/terminus_sleep/Registration.h"
#include "util/TerminusCredentialStore.h"
#endif
#include "activities/settings/RecoveryMenuActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "core/CoreBootstrap.h"
#include "core/features/FeatureLifecycle.h"
#include "core/features/FeatureModules.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"
#include "network/background/BackgroundServerPolicy.h"
#include "network/background/BackgroundWebServer.h"
#include "network/background/BackgroundWifiCoordinator.h"
#include "network/background/BackgroundWifiService.h"
#include "network/ota/SerialOtaSession.h"
#include "network/server/SettingsApi.h"
#include "network/wifi/WifiUtil.h"
#include "util/ButtonNavigator.h"
#include "util/FactoryResetUtils.h"
#include "util/FirmwareUpdateUtil.h"
#include "util/LibraryShelfStore.h"
#include "util/RecentBooksStore.h"
#include "util/ScreenshotUtil.h"
#if ENABLE_WIFI_CLOCK
#include "util/TimeSync.h"
#endif
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

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// SILENT RESTART FEATURE - This is a restart without showing the boot splash screen -- TODO: This doesn't seem to work
// for us void silentRestart() {
//   silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
//   silentRebootMagic = SILENT_REBOOT_MAGIC;
//   LOG_DBG("MAIN", "Silent restart (target=home)");
//   delay(50);
//   ESP.restart();
// }

// void silentRestartToReader() {
//   silentRebootTarget = SILENT_REBOOT_TARGET_READER;
//   silentRebootMagic = SILENT_REBOOT_MAGIC;
//   LOG_DBG("MAIN", "Silent restart (target=reader)");
//   delay(50);
//   ESP.restart();
// }

// ── Serial firmware OTA over the USB-CDC CMD: channel ──────────────────────
// Standalone replacement for the removed USB-mass-storage serial protocol. The
// host streams a firmware image as base64 chunks over the same unconditional
// CMD: channel the device_walk test rig uses; we write to the inactive OTA
// partition and flip the boot pointer on OTA_END (see handleSerialOtaCommand).
// True only between OTA_BEGIN and OTA_END; silentRestart() consults it so a
// heap-defrag reboot never aborts an in-flight flash write.
static SerialOtaSession s_serialOtaSession;
bool serialOtaInProgress() { return s_serialOtaSession.inProgress(); }

bool silentRestart(uint32_t target) {
  if (deepSleepInProgress) return false;  // sleeping supersedes the heap-defrag reboot
  if (serialOtaInProgress()) {
    // Never reboot mid-OTA: esp_ota_end has not run, so a restart here aborts the
    // flash write mid-stream and drops the host's upload. OTA_END issues its own
    // restart once the image is finalized.
    LOG_WRN("MAIN", "Silent restart suppressed: serial OTA in progress");
    return false;
  }
  silentRebootTarget = target;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=%d)", target);
  delay(50);
  ESP.restart();
  return true;
}

// Resume straight back into the currently open book (APP_STATE.openEpubPath) with
// a freshly defragmented heap. The boot dispatcher routes SILENT_REBOOT_TARGET_READER
// in setup() (see the silentReboot branch there).
bool silentRestartToReader() { return silentRestart(SILENT_REBOOT_TARGET_READER); }

static bool backgroundServerKeepsWifiWhileAwake() {
  if (SETTINGS.keepsBackgroundServerOnWifiWhileAwake()) {
    return true;
  }
  return core::FeatureModules::hasCapability(core::Capability::BackgroundServer) && SETTINGS.backgroundServerOnCharge &&
         gpio.isUsbConnected();
}

// TODO: This can be refactored to be leaner
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
constexpr uint32_t kSafeModeSleepHoldMs = 1500;

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

#if ENABLE_TIMED_SLEEP_REFRESH
static uint8_t effectiveTimedRefreshSleepMode() {
  if (SETTINGS.sleepScreenSplit == CrossPointSettings::SLEEP_SPLIT_SMART) {
    return APP_STATE.lastSleepFromReader ? SETTINGS.sleepScreenReader : SETTINGS.sleepScreenHome;
  }
  return SETTINGS.sleepScreen;
}

// Returns true if the effective sleep mode is one that benefits from a timed refresh.
static bool timedRefreshHasRenderableMode() {
  const uint8_t sleepMode = effectiveTimedRefreshSleepMode();
#if ENABLE_TERMINUS_SLEEP
  if (SETTINGS.terminusSleepEnabled && TERMINUS_STORE.hasCredentials()) {
    return true;
  }
#endif
#if ENABLE_ROMAN_CLOCK_SLEEP
  if (sleepMode == CrossPointSettings::SLEEP_SCREEN_MODE::ROMAN_CLOCK_SLEEP) {
    return true;
  }
#endif
#if ENABLE_HAIKU_CLOCK
  if (sleepMode == CrossPointSettings::SLEEP_SCREEN_MODE::HAIKU_CLOCK_SLEEP) {
    return true;
  }
#endif
  return false;
}

// Called from setup() on ESP_SLEEP_WAKEUP_TIMER: silently re-render the sleep screen
// and go back to deep sleep. Never returns.
[[noreturn]] static void runTimedSleepRefresh() {
  LOG_DBG("MAIN", "Timed sleep refresh: woke from timer");

  // Determine what WiFi work is needed before we can render.
  const bool needsTerminusFetch =
#if ENABLE_TERMINUS_SLEEP
      SETTINGS.terminusSleepEnabled && TERMINUS_STORE.hasCredentials();
#else
      false;
#endif
  const bool needsNtpSync =
#if ENABLE_ROMAN_CLOCK_SLEEP || ENABLE_HAIKU_CLOCK
      [] {
        const uint8_t sleepMode = effectiveTimedRefreshSleepMode();
        return sleepMode == CrossPointSettings::SLEEP_SCREEN_MODE::ROMAN_CLOCK_SLEEP ||
               sleepMode == CrossPointSettings::SLEEP_SCREEN_MODE::HAIKU_CLOCK_SLEEP;
      }();
#else
      false;
#endif

  if (needsTerminusFetch || needsNtpSync) {
    BackgroundWifiCoordinator& wifiCoord = BackgroundWifiCoordinator::getInstance();
    if (wifiCoord.beginTimedSleepAutoConnect("TREFRESH")) {
      if (wifiCoord.waitForStaConnection(20000)) {
        if (needsNtpSync) {
          if (!TimeSync::syncTimeWithNtpLowMemory()) {
            LOG_WRN("TREFRESH", "NTP sync failed — rendering with potentially stale time");
          }
        }
#if ENABLE_TERMINUS_SLEEP
        if (needsTerminusFetch) {
          constexpr uint32_t kFetchCapMs = 60000;
          if (!features::terminus_sleep::startTrmnlFetchAndWait(kFetchCapMs)) {
            LOG_WRN("TREFRESH", "Terminus fetch failed or timed out — rendering stale image");
          }
        }
#endif
      } else {
        LOG_WRN("TREFRESH", "WiFi association timed out — skipping network work");
      }
      wifiCoord.endTimedSleepWifi();
    }
  }

  // Re-render the sleep screen via SleepActivity::onEnter() — single render path
  // covers CUSTOM (Terminus), ROMAN_CLOCK_SLEEP, HAIKU_CLOCK_SLEEP, and fallbacks.
  {
    SleepActivity sleepActivity(renderer, mappedInputManager);
    sleepActivity.onEnter();
  }

  display.deepSleep();
  const uint64_t timerMicros =
      (gpio.isUsbConnected() && timedRefreshHasRenderableMode() && SETTINGS.getTimedRefreshIntervalMicros() > 0)
          ? SETTINGS.getTimedRefreshIntervalMicros()
          : 0;
  powerManager.startDeepSleep(gpio, timerMicros);
  __builtin_unreachable();
}
#endif  // ENABLE_TIMED_SLEEP_REFRESH

void enterDeepSleep() {
  HalPowerManager::Lock powerLock;
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  BackgroundWifiCoordinator::getInstance().onPrepareDeepSleep();

  if (!APP_STATE.saveToFile()) {
    LOG_WRN("MAIN", "Failed to persist app state before deep sleep");
  }

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;

  activityManager.goToSleep();

  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  uint64_t timerMicros = 0;
#if ENABLE_TIMED_SLEEP_REFRESH
  if (gpio.isUsbConnected() && timedRefreshHasRenderableMode() && SETTINGS.getTimedRefreshIntervalMicros() > 0) {
    timerMicros = SETTINGS.getTimedRefreshIntervalMicros();
    LOG_DBG("MAIN", "Arming timer wakeup: %" PRIu64 " µs", timerMicros);
  }
#endif
  logSerial.flush();  // drain USB CDC TX before CPU halts
  powerManager.startDeepSleep(gpio, timerMicros);
}

bool setupDisplayAndFonts() {
  if (displayAndFontsReady) {
    return true;
  }

  display.begin();
  renderer.begin();

  // Apply the global UI orientation so every activity (boot, home, settings, ...)
  // inherits it. No-op (Portrait) when ENABLE_GLOBAL_LANDSCAPE is off.
  OrientationManager::applyUiOrientation(renderer);

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

unsigned long lastActivityTime = 0;

void setup() {
  lastActivityTime = millis();
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

  SETTINGS.loadFromFile();
  HalSystem::checkPanic();
  invalidateSleepImageCache();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
#if ENABLE_WIFI_CLOCK
  TimeSync::restorePersistedTime();
#endif
  core::FeatureLifecycle::onSettingsLoaded(renderer);
  WIFI_STORE.loadFromFile();
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
#if ENABLE_TIMED_SLEEP_REFRESH
    case HalGPIO::WakeupReason::TimerRefresh:
      LOG_DBG("MAIN", "Wakeup reason: Timer (timed sleep refresh)");
      break;
#endif
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

#if ENABLE_TIMED_SLEEP_REFRESH
  if (wakeupReason == HalGPIO::WakeupReason::TimerRefresh) {
    runTimedSleepRefresh();
  }
#endif

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
  LIBRARY_SHELF.loadFromFile();

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

  BackgroundWifiCoordinator::getInstance().attemptBootAutoConnect();

  waitForPowerRelease();
}

#ifndef SIMULATOR
struct SerialOtaFlashCtx {
  OtaFlashOpsState state;
  esp_ota_handle_t handle = 0;
  const esp_partition_t* partition = nullptr;
};

static SerialOtaFlashCtx s_serialOtaFlashCtx;

static void serialOtaEmitReply(void* ctx, const char* line) {
  (void)ctx;
  logSerial.print(line);
}

static void serialOtaAbortFlash(void* ctx) {
  auto* flash = static_cast<SerialOtaFlashCtx*>(ctx);
  if (flash->handle != 0) {
    esp_ota_abort(flash->handle);
    flash->handle = 0;
  }
}

static bool serialOtaBeginFlash(void* ctx) {
  auto* flash = static_cast<SerialOtaFlashCtx*>(ctx);
  flash->partition = esp_ota_get_next_update_partition(nullptr);
  if (!flash->partition) {
    strncpy(flash->state.lastError, "no partition", sizeof(flash->state.lastError));
    flash->state.lastError[sizeof(flash->state.lastError) - 1] = '\0';
    return false;
  }
  const esp_err_t err = esp_ota_begin(flash->partition, OTA_WITH_SEQUENTIAL_WRITES, &flash->handle);
  if (err != ESP_OK) {
    strncpy(flash->state.lastError, esp_err_to_name(err), sizeof(flash->state.lastError));
    flash->state.lastError[sizeof(flash->state.lastError) - 1] = '\0';
    return false;
  }
  return true;
}

static bool serialOtaWriteFlash(void* ctx, const uint8_t* data, const size_t len) {
  auto* flash = static_cast<SerialOtaFlashCtx*>(ctx);
  const esp_err_t err = esp_ota_write(flash->handle, data, len);
  if (err != ESP_OK) {
    strncpy(flash->state.lastError, esp_err_to_name(err), sizeof(flash->state.lastError));
    flash->state.lastError[sizeof(flash->state.lastError) - 1] = '\0';
    return false;
  }
  return true;
}

static bool serialOtaEndFlash(void* ctx) {
  auto* flash = static_cast<SerialOtaFlashCtx*>(ctx);
  const esp_err_t endErr = esp_ota_end(flash->handle);
  flash->handle = 0;
  if (endErr != ESP_OK) {
    strncpy(flash->state.lastError, esp_err_to_name(endErr), sizeof(flash->state.lastError));
    flash->state.lastError[sizeof(flash->state.lastError) - 1] = '\0';
    return false;
  }
  const esp_err_t bootErr = esp_ota_set_boot_partition(flash->partition);
  if (bootErr != ESP_OK) {
    strncpy(flash->state.lastError, esp_err_to_name(bootErr), sizeof(flash->state.lastError));
    flash->state.lastError[sizeof(flash->state.lastError) - 1] = '\0';
    return false;
  }
  return true;
}

static const OtaFlashOps kSerialOtaFlashOps = {
    .begin = serialOtaBeginFlash,
    .write = serialOtaWriteFlash,
    .end = serialOtaEndFlash,
    .abort = serialOtaAbortFlash,
    .ctx = &s_serialOtaFlashCtx,
};

bool handleSerialOtaCommand(const String& cmd) {
  const SerialOtaHandleResult result =
      s_serialOtaSession.handleCommand(cmd, kSerialOtaFlashOps, millis(), serialOtaEmitReply, nullptr);
  if (result.restartRequested) {
    logSerial.flush();
    delay(200);
    ESP.restart();
  }
  return result.handled;
}
#endif  // SIMULATOR

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;
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

  mappedInputManager.update();
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
      } else if (cmd == "PING") {
        logSerial.printf("PONG\n");
      } else if (cmd.startsWith("BTN:")) {
        // Test harness: inject a logical button press; consumed by the current
        // activity exactly like a physical press (orientation/remap aware).
        static const struct {
          const char* name;
          MappedInputManager::Button button;
        } kBtnMap[] = {
            {"BACK", MappedInputManager::Button::Back},
            {"CONFIRM", MappedInputManager::Button::Confirm},
            {"LEFT", MappedInputManager::Button::Left},
            {"RIGHT", MappedInputManager::Button::Right},
            {"UP", MappedInputManager::Button::Up},
            {"DOWN", MappedInputManager::Button::Down},
            {"PAGEBACK", MappedInputManager::Button::PageBack},
            {"PAGEFWD", MappedInputManager::Button::PageForward},
        };
        const String name = cmd.substring(4);
        // strcasecmp instead of String::equalsIgnoreCase: the simulator's String
        // mock returns void from equalsIgnoreCase, and c_str() works on both.
        const auto it = std::find_if(std::begin(kBtnMap), std::end(kBtnMap),
                                     [&](const auto& e) { return strcasecmp(name.c_str(), e.name) == 0; });
        const bool matched = it != std::end(kBtnMap);
        if (matched) mappedInputManager.injectVirtualActivation(it->button);
        logSerial.printf(matched ? "BTN_OK:%s\n" : "BTN_ERR:%s\n", name.c_str());
      } else if (cmd.startsWith("WIFICRED:")) {
        // Test harness: set or update a saved WiFi credential and persist it.
        // Format: CMD:WIFICRED:<ssid>\t<password>
        const String payload = cmd.substring(9);
        const int sep = payload.indexOf('\t');
        if (sep <= 0) {
          logSerial.printf("WIFICRED_ERR:format\n");
        } else {
          const std::string ssid(payload.substring(0, sep).c_str());
          const std::string password(payload.substring(sep + 1).c_str());
          const bool ok = WIFI_STORE.addCredential(ssid, password);
          logSerial.printf(ok ? "WIFICRED_OK:%s\n" : "WIFICRED_ERR:%s\n", ssid.c_str());
        }
      } else if (cmd.startsWith("SETTINGS:")) {
        // Test harness: apply settings by key, same payload as POST
        // /api/settings. Format: CMD:SETTINGS:{"key":value,...}
        // applySettingsJson rebuilds the settings list (~tens of KB burst), so
        // require the same heap floor as the web handlers before attempting.
        constexpr uint32_t kMinHeapForSettingsApply = 48000;
        if (ESP.getFreeHeap() < kMinHeapForSettingsApply) {
          logSerial.printf("SETTINGS_ERR:low heap (%u free)\n", ESP.getFreeHeap());
        } else {
          const auto result = network::applySettingsJson(cmd.substring(9));
          logSerial.printf(result.ok() ? "SETTINGS_OK:%d applied\n" : "SETTINGS_ERR:http %d\n",
                           result.ok() ? result.appliedCount : result.statusCode);
        }
#ifndef SIMULATOR
      } else if (handleSerialOtaCommand(cmd)) {
        // Firmware OTA over serial (CMD:OTA_BEGIN / OTA_DATA / OTA_END / OTA_ABORT).
#endif
      }
    }
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

  BackgroundWifiCoordinator::getInstance().reconcile(BackgroundWifiReconcileContext{
      .blockedByActivity = activityManager.blocksBackgroundServer(),
      .usbBackgroundServerRunning = backgroundServer.isRunning(),
  });
#if ENABLE_WIFI_CLOCK
  TimeSync::loop(hasStaWifiConnection());
#endif
  refreshGlobalStatusBarOnWifiChange();
#if ENABLE_WIFI_CLOCK
  refreshClockOnTick();
#endif

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep() ||
      features::status_overlay::preventsAutoSleep() ||  // cppcheck-suppress knownConditionTrueFalse
      backgroundServer.shouldPreventAutoSleep() || serialOtaInProgress()) {
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

#ifndef SIMULATOR
  if (s_serialOtaSession.checkIdleTimeout(kSerialOtaFlashOps, millis())) {
    LOG_WRN("MAIN", "Serial OTA idle >30s; aborting and unlatching");
  }
#endif

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

#if ENABLE_DOUBLE_TAP_ACTION
  // Configurable double-tap power button action.
  // Global actions (FORCE_REFRESH, SLEEP) are dispatched here so they work on any screen.
  // Reader-specific actions are handled in reader activities via executeDoubleTapAction().
  if (mappedInputManager.consumePowerDoubleTap()) {
    using S = CrossPointSettings;
    switch (static_cast<S::SHORT_PWRBTN>(SETTINGS.doubleTapPwrBtn)) {
      case S::FORCE_REFRESH: {
        LOG_DBG("MAIN", "Double-tap screen refresh");
        RenderLock lock;
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        break;
      }
      case S::SLEEP:
        enterDeepSleep();
        break;
      default:
        // Reader-specific actions are handled in EpubReaderActivity::executeDoubleTapAction().
        break;
    }
  }
#endif

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
