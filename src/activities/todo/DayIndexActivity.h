#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "../Activity.h"

struct DayIndexEntry {
  std::string isoDate;
  int openCount = 0;
  int doneCount = 0;
};

class DayIndexActivity final : public Activity {
 public:
  DayIndexActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void* onBackCtx, void (*onBack)(void*));

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;

 private:
  void* onBackCtx;
  void (*onBack)(void*);

  std::vector<DayIndexEntry> entries;
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool skipInitialInput = true;
  std::string today;

  void loadEntries();
  void openSelectedDay();
  void renderScreen();
};
