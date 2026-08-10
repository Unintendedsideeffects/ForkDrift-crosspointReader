#pragma once

#include <FeatureFlags.h>

#if ENABLE_CLAUDE_BRIDGE

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "features/claude_bridge/Registration.h"

// Four-button adaptation of claudeq's AskUserQuestion screen:
// Back falls through to Claude's terminal picker, Confirm selects the focused
// option, and Up/Down move through the options. Multi-question prompts advance
// one question at a time.
class ClaudeBridgeActivity final : public Activity {
 public:
  ClaudeBridgeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onGoBack);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool preventAutoSleep() override { return true; }
  bool blocksBackgroundServer() override { return false; }

 private:
  std::function<void()> onGoBack;
  features::claude_bridge::PendingRequest current;
  std::vector<features::claude_bridge::QuestionAnswer> answers;
  size_t questionIndex = 0;
  int selectedOption = 0;

  void adoptPending(const features::claude_bridge::PendingRequest& next);
  void clearCurrent();
  void cancelCurrent();
  void selectCurrentOption();
};

#endif
