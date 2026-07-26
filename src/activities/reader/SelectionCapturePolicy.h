#pragma once

#include <cstdint>
#include <vector>

namespace selection_capture {

enum class Action : int8_t {
  Dictionary = 0,
  Anki = 1,
  BookNotes = 2,
  Highlight = 3,
  RemoveHighlight = 4,
  Close = 5,
};

struct ActionBuildOptions {
  bool dictionary = false;
  bool anki = false;
  bool annotations = false;
  bool highlight = false;
  bool removeHighlight = false;
};

std::vector<Action> buildActions(const ActionBuildOptions& options);
int findActionIndex(const std::vector<Action>& actions, Action preferred);
Action preferredAfterNotesSuccess();
Action preferredAfterNotesFailure();
// Deliberately invokes persistence only for the explicit Highlight action.
// The function-pointer seam keeps policy tests independent of AnnotationStore
// and avoids std::function allocation on the firmware path.
bool executeHighlight(Action action, void* context, bool (*persist)(void*));

}  // namespace selection_capture
