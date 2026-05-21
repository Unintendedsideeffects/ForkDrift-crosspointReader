#pragma once

#include "activities/Activity.h"

class ResetSettingsActivity final : public Activity {
 public:
  explicit ResetSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ResetSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, RESETTING, SUCCESS, FAILED };

  State state = WARNING;

  void goBack() { finish(); }
  void resetSettings();
};
