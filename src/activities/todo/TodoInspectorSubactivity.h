#pragma once

#include "../Activity.h"
#include "TodoItem.h"

class DayDetailActivity;

class TodoInspectorSubactivity final : public Activity {
 public:
  enum class Action { EditText, CyclePriority, SetTime, ClearTime, MoveUp, MoveDown, Delete, Cancel };

  TodoInspectorSubactivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DayDetailActivity& parent,
                           int taskIndex);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;

 private:
  DayDetailActivity& parent;
  int taskIndex;
  int selectedAction = 0;

  void renderScreen();
  void applyAction(Action action);
};
