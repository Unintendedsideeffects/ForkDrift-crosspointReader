#include "TodoInspectorSubactivity.h"

#include <I18n.h>

#include "DayDetailActivity.h"
#include "MappedInputManager.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "fontIds.h"

namespace {
constexpr int ROW_HEIGHT = 34;
constexpr int TOP_Y = 50;
constexpr int LEFT_X = 20;

const char* actionLabel(const TodoInspectorSubactivity::Action action) {
  switch (action) {
    case TodoInspectorSubactivity::Action::EditText:
      return tr(STR_TODO_INSPECTOR_EDIT);
    case TodoInspectorSubactivity::Action::CyclePriority:
      return tr(STR_TODO_INSPECTOR_PRIORITY);
    case TodoInspectorSubactivity::Action::SetTime:
      return tr(STR_TODO_INSPECTOR_SET_TIME);
    case TodoInspectorSubactivity::Action::ClearTime:
      return tr(STR_TODO_INSPECTOR_CLEAR_TIME);
    case TodoInspectorSubactivity::Action::MoveUp:
      return tr(STR_TODO_INSPECTOR_MOVE_UP);
    case TodoInspectorSubactivity::Action::MoveDown:
      return tr(STR_TODO_INSPECTOR_MOVE_DOWN);
    case TodoInspectorSubactivity::Action::NewTask:
      return tr(STR_TODO_NEW_TASK);
    case TodoInspectorSubactivity::Action::NewSection:
      return tr(STR_TODO_NEW_SECTION);
    case TodoInspectorSubactivity::Action::Delete:
      return tr(STR_TODO_INSPECTOR_DELETE);
    case TodoInspectorSubactivity::Action::Cancel:
      return tr(STR_CANCEL);
  }
  return "";
}

constexpr TodoInspectorSubactivity::Action kActions[] = {
    TodoInspectorSubactivity::Action::EditText, TodoInspectorSubactivity::Action::CyclePriority,
    TodoInspectorSubactivity::Action::SetTime,  TodoInspectorSubactivity::Action::ClearTime,
    TodoInspectorSubactivity::Action::MoveUp,   TodoInspectorSubactivity::Action::MoveDown,
    TodoInspectorSubactivity::Action::NewTask,  TodoInspectorSubactivity::Action::NewSection,
    TodoInspectorSubactivity::Action::Delete,   TodoInspectorSubactivity::Action::Cancel,
};
}  // namespace

TodoInspectorSubactivity::TodoInspectorSubactivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   DayDetailActivity& parent, const int taskIndex)
    : Activity("TodoInspector", renderer, mappedInput), parent(parent), taskIndex(taskIndex) {}

void TodoInspectorSubactivity::onEnter() {
  Activity::onEnter();
  selectedAction = 0;
  requestUpdate();
}

void TodoInspectorSubactivity::onExit() { Activity::onExit(); }

void TodoInspectorSubactivity::applyAction(const Action action) {
  parent.focusTaskIndex(taskIndex);
  switch (action) {
    case Action::EditText:
      parent.editSelectedTaskText();
      return;
    case Action::CyclePriority:
      parent.cycleSelectedPriority(1);
      break;
    case Action::SetTime:
      parent.closeInspector();
      parent.editSelectedDueTime();
      return;
    case Action::ClearTime:
      parent.setSelectedDueMinutes(0);
      break;
    case Action::MoveUp:
      parent.moveSelectedTask(-1);
      break;
    case Action::MoveDown:
      parent.moveSelectedTask(1);
      break;
    case Action::NewTask:
      parent.closeInspector();
      parent.addNewEntry(false);
      return;
    case Action::NewSection:
      parent.closeInspector();
      parent.addNewEntry(true);
      return;
    case Action::Delete:
      parent.deleteSelectedTask();
      break;
    case Action::Cancel:
      parent.closeInspector();
      return;
  }
  parent.closeInspector();
}

void TodoInspectorSubactivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    parent.closeInspector();
    return;
  }

  const int actionCount = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) && selectedAction > 0) {
    selectedAction--;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) && selectedAction < actionCount - 1) {
    selectedAction++;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    applyAction(kActions[static_cast<size_t>(selectedAction)]);
  }
}

void TodoInspectorSubactivity::render(Activity::RenderLock&& lock) { renderScreen(); }

void TodoInspectorSubactivity::renderScreen() {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_TODO_INSPECTOR_TITLE), true, EpdFontFamily::BOLD);

  const int actionCount = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));
  for (int i = 0; i < actionCount; ++i) {
    const int y = TOP_Y + i * ROW_HEIGHT;
    const bool selected = i == selectedAction;
    if (selected) {
      renderer.fillRect(0, y, renderer.getScreenWidth(), ROW_HEIGHT);
    }
    renderer.drawText(UI_10_FONT_ID, LEFT_X, y + 8, actionLabel(kActions[static_cast<size_t>(i)]), !selected);
  }

  const auto labels = mappedInput.mapLabels("Back", "Select", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
