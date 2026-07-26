#include "SelectionCapturePolicy.h"

namespace selection_capture {

std::vector<Action> buildActions(const ActionBuildOptions& options) {
  std::vector<Action> actions;
  actions.reserve(6);
  if (options.dictionary) {
    actions.push_back(Action::Dictionary);
  }
  if (options.anki) {
    actions.push_back(Action::Anki);
  }
  actions.push_back(Action::BookNotes);
  if (options.annotations && options.highlight) {
    actions.push_back(Action::Highlight);
  }
  if (options.annotations && options.removeHighlight) {
    actions.push_back(Action::RemoveHighlight);
  }
  actions.push_back(Action::Close);
  return actions;
}

int findActionIndex(const std::vector<Action>& actions, const Action preferred) {
  for (size_t i = 0; i < actions.size(); ++i) {
    if (actions[i] == preferred) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

Action preferredAfterNotesSuccess() { return Action::BookNotes; }

Action preferredAfterNotesFailure() { return Action::BookNotes; }

bool executeHighlight(const Action action, void* const context, bool (*persist)(void*)) {
  return action == Action::Highlight && persist != nullptr && persist(context);
}

}  // namespace selection_capture
