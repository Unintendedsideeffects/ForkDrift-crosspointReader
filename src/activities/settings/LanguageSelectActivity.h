#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <functional>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public Activity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LanguageSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();

  void onBack() { finish(); }
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  // Languages compiled into this build, in display order (SORTED_LANGUAGE_INDICES
  // filtered by I18n::isLanguageAvailable). Fixed-size: at most one byte per language.
  uint8_t items[getLanguageCount()] = {};
  uint8_t totalItems = 0;
};
