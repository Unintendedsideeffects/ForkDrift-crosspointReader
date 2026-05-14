#pragma once
#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include <functional>
#include <string>
#include <utility>

#include "activities/Activity.h"

class FullScreenMessageActivity final : public Activity {
  std::string text;
  EpdFontFamily::Style style;
  std::function<void()> onDismiss;
  HalDisplay::RefreshMode refreshMode;

 public:
  explicit FullScreenMessageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string text,
                                     const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                     std::function<void()> onDismiss = nullptr,
                                     const HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH)
      : Activity("FullScreenMessage", renderer, mappedInput),
        text(std::move(text)),
        style(style),
        onDismiss(std::move(onDismiss)),
        refreshMode(refreshMode) {}
  void onEnter() override;
  void loop() override;
};
