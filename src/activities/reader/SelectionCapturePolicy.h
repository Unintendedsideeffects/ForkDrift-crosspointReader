#pragma once

#include <cstdint>
#include <vector>

namespace selection_capture {

enum class Action : int8_t {
  Dictionary = 0,
  Anki = 1,
  BookNotes = 2,
  RemoveHighlight = 3,
  Close = 4,
};

struct ActionBuildOptions {
  bool dictionary = false;
  bool anki = false;
  bool annotations = false;
  bool removeHighlight = false;
};

std::vector<Action> buildActions(const ActionBuildOptions& options);
int findActionIndex(const std::vector<Action>& actions, Action preferred);
Action preferredAfterNotesSuccess();
Action preferredAfterNotesFailure();

}  // namespace selection_capture
