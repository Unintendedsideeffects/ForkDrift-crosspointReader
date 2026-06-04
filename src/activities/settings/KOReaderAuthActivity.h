#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>

#include "activities/Activity.h"

/**
 * Tests KOReader Sync credentials (koreader_sync feature) over WiFi against the sync server.
 */
class KOReaderAuthActivity final : public Activity {
 public:
  explicit KOReaderAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KOReaderAuth", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == AUTHENTICATING; }

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;

  // Background auth task (already-connected path). onExit() must wait for it to
  // finish before the heap-allocated activity is destroyed (use-after-free).
  TaskHandle_t authTaskHandle = nullptr;
  std::atomic<bool> authTaskExited{false};

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
