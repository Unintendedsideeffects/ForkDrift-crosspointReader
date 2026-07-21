#include "activities/reader/SelectionCapturePolicy.h"
#include "doctest/doctest.h"

TEST_CASE("selection capture policy prefers highlight after notes success") {
  selection_capture::ActionBuildOptions options;
  options.anki = true;
  options.annotations = true;
  const auto actions = selection_capture::buildActions(options);
  const auto preferred = selection_capture::preferredAfterNotesSuccess(true);
  CHECK(preferred == selection_capture::Action::Highlight);
  CHECK(selection_capture::findActionIndex(actions, preferred) == 2);
}

TEST_CASE("selection capture policy keeps book notes preferred after failure") {
  selection_capture::ActionBuildOptions options;
  options.annotations = true;
  const auto actions = selection_capture::buildActions(options);
  const auto preferred = selection_capture::preferredAfterNotesFailure();
  CHECK(preferred == selection_capture::Action::BookNotes);
  CHECK(selection_capture::findActionIndex(actions, preferred) == 0);
}

TEST_CASE("selection capture policy omits highlight when annotations disabled") {
  selection_capture::ActionBuildOptions options;
  options.anki = true;
  options.annotations = false;
  const auto actions = selection_capture::buildActions(options);
  CHECK(actions.size() == 2);
  CHECK(selection_capture::preferredAfterNotesSuccess(false) == selection_capture::Action::BookNotes);
}
