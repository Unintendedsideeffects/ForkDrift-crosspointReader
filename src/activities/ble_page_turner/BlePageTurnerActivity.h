#pragma once

#include "activities/Activity.h"
#include "features/ble_page_turner/BlePageTurner.h"

class BlePageTurnerActivity : public Activity {
 public:
  explicit BlePageTurnerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~BlePageTurnerActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  BlePageTurner blePageTurner;
  bool isConnected = false;
};
