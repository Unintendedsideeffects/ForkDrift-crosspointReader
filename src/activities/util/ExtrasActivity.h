#pragma once

#include <vector>

#include "activities/Activity.h"
#include "activities/home/HomeActivity.h"
#include "util/ButtonNavigator.h"

// The "Extras" submenu behind HomeMenuId::Extras. Purely presentational: it
// renders the ordered bucket it is handed and reports the chosen index back as
// a ListPickerResult. Activation stays in HomeActivity::activateMenuId(), which
// runs once Home is current again — launching from here would push the started
// activity onto this submenu instead of Home.
class ExtrasActivity final : public Activity {
 public:
  ExtrasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<HomeMenuId> items);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::vector<HomeMenuId> items;
  std::vector<std::string> labels;  // resolved once in onEnter(); drawList needs stable c_str()s
  int selectedIndex = 0;
};
