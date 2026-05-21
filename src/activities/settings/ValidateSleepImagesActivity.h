#pragma once

#include <functional>

#include "activities/ActivityWithSubactivity.h"

class ValidateSleepImagesActivity final : public ActivityWithSubactivity {
 public:
  explicit ValidateSleepImagesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::function<void()>& goBack)
      : ActivityWithSubactivity("ValidateSleepImages", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum State { SCANNING, DONE };

  State state = SCANNING;
  bool scanStarted = false;
  const std::function<void()> goBack;

  int validCount = 0;
  int invalidCount = 0;

  void render(RenderLock&&) override;
};
