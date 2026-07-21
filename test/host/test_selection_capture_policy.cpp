#include "activities/reader/SelectionCapturePolicy.h"
#include "doctest/doctest.h"

TEST_CASE("selection capture policy single-word full menu order") {
  selection_capture::ActionBuildOptions options;
  options.dictionary = true;
  options.anki = true;
  options.annotations = true;
  options.removeHighlight = true;
  const auto actions = selection_capture::buildActions(options);
  REQUIRE(actions.size() == 5);
  CHECK(actions[0] == selection_capture::Action::Dictionary);
  CHECK(actions[1] == selection_capture::Action::Anki);
  CHECK(actions[2] == selection_capture::Action::BookNotes);
  CHECK(actions[3] == selection_capture::Action::RemoveHighlight);
  CHECK(actions[4] == selection_capture::Action::Close);
}

TEST_CASE("selection capture policy multi-word omits dictionary") {
  selection_capture::ActionBuildOptions options;
  options.dictionary = false;
  options.anki = true;
  options.annotations = true;
  const auto actions = selection_capture::buildActions(options);
  REQUIRE(actions.size() == 3);
  CHECK(actions[0] == selection_capture::Action::Anki);
  CHECK(actions[1] == selection_capture::Action::BookNotes);
  CHECK(actions[2] == selection_capture::Action::Close);
}

TEST_CASE("selection capture policy keeps book notes preferred after notes") {
  selection_capture::ActionBuildOptions options;
  options.annotations = true;
  const auto actions = selection_capture::buildActions(options);
  const auto preferred = selection_capture::preferredAfterNotesSuccess();
  CHECK(preferred == selection_capture::Action::BookNotes);
  CHECK(selection_capture::findActionIndex(actions, preferred) == 0);
}

TEST_CASE("selection capture policy keeps book notes preferred after failure") {
  selection_capture::ActionBuildOptions options;
  options.annotations = true;
  const auto actions = selection_capture::buildActions(options);
  const auto preferred = selection_capture::preferredAfterNotesFailure();
  CHECK(preferred == selection_capture::Action::BookNotes);
  CHECK(selection_capture::findActionIndex(actions, preferred) == 0);
}
