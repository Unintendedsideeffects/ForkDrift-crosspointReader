#pragma once

#include <string>
#include <vector>

#include "Activity.h"
#include "util/AnkiStore.h"
#include "util/ButtonNavigator.h"

class AnkiActivity final : public Activity {
 public:
  explicit AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectedIndex = 0;
  bool showingBack = false;
  ButtonNavigator buttonNavigator;
  std::vector<util::AnkiCard> cards;  // snapshot taken in onEnter()
  void nextSrsCard();
};
