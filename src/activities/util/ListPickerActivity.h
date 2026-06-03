#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/ActivityResult.h"
#include "util/ButtonNavigator.h"

class ListPickerActivity final : public Activity {
 public:
  explicit ListPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, StrId titleId,
                              std::vector<std::string> items, int initialIndex)
      : Activity("ListPicker", renderer, mappedInput),
        titleId_(titleId),
        items_(std::move(items)),
        selectedIndex_(initialIndex) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  StrId titleId_;
  std::vector<std::string> items_;
  int selectedIndex_;
  ButtonNavigator buttonNavigator_;
};
