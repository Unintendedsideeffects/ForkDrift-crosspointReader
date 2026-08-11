#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>

#include "activities/Activity.h"
#include "network/ota/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    LOADING_FEATURE_STORE,
    SELECTING_FEATURE_STORE_BUNDLE,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  enum class OtaWorkerCmd { NONE, LOAD_CATALOG, CHECK_UPDATE, INSTALL_UPDATE };

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  size_t selectedBundleIndex = 0;
  bool usingFeatureStore = false;
  unsigned long restartAtMs = 0;
  // Translated detail for the FAILED screen. getLastError() carries an
  // untranslated diagnostic string; where we can name the cause precisely we
  // show a localized line instead. nullptr means fall back to getLastError().
  const char* failedDetail = nullptr;

  // Worker task for background OTA operations
  TaskHandle_t otaWorkerTaskHandle = nullptr;
  std::atomic<bool> workerExitRequested{false};
  std::atomic<bool> workerHasExited{false};
  std::atomic<OtaWorkerCmd> workerCmd{OtaWorkerCmd::NONE};

  void onWifiSelectionComplete(bool success);
  static void otaWorkerTrampoline(void* param);
  void otaWorkerLoop();
  void dispatchWorker(OtaWorkerCmd cmd);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state == LOADING_FEATURE_STORE || state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS;
  }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  bool blocksBackgroundServer() override { return true; }
};
