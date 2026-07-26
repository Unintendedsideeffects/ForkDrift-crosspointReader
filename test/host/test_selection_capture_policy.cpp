#include "activities/reader/SelectionCapturePolicy.h"
#include "doctest/doctest.h"

TEST_CASE("selection capture policy single-word full menu order") {
  selection_capture::ActionBuildOptions options;
  options.dictionary = true;
  options.anki = true;
  options.annotations = true;
  options.highlight = true;
  options.removeHighlight = true;
  const auto actions = selection_capture::buildActions(options);
  REQUIRE(actions.size() == 6);
  CHECK(actions[0] == selection_capture::Action::Dictionary);
  CHECK(actions[1] == selection_capture::Action::Anki);
  CHECK(actions[2] == selection_capture::Action::BookNotes);
  CHECK(actions[3] == selection_capture::Action::Highlight);
  CHECK(actions[4] == selection_capture::Action::RemoveHighlight);
  CHECK(actions[5] == selection_capture::Action::Close);
}

TEST_CASE("selection capture policy multi-word omits dictionary") {
  selection_capture::ActionBuildOptions options;
  options.dictionary = false;
  options.anki = true;
  options.annotations = true;
  options.highlight = true;
  const auto actions = selection_capture::buildActions(options);
  REQUIRE(actions.size() == 4);
  CHECK(actions[0] == selection_capture::Action::Anki);
  CHECK(actions[1] == selection_capture::Action::BookNotes);
  CHECK(actions[2] == selection_capture::Action::Highlight);
  CHECK(actions[3] == selection_capture::Action::Close);
}

TEST_CASE("selection capture policy never persists without explicit Highlight") {
  struct Spy {
    int writes = 0;
    bool result = true;
  } spy;
  const auto persist = [](void* context) {
    auto& state = *static_cast<Spy*>(context);
    ++state.writes;
    return state.result;
  };

  const selection_capture::Action nonHighlightActions[] = {
      selection_capture::Action::Dictionary, selection_capture::Action::Anki,
      selection_capture::Action::BookNotes,  selection_capture::Action::RemoveHighlight,
      selection_capture::Action::Close,
  };
  for (const auto action : nonHighlightActions) {
    CHECK_FALSE(selection_capture::executeHighlight(action, &spy, persist));
  }
  CHECK(spy.writes == 0);

  CHECK(selection_capture::executeHighlight(selection_capture::Action::Highlight, &spy, persist));
  CHECK(spy.writes == 1);

  spy.result = false;
  CHECK_FALSE(selection_capture::executeHighlight(selection_capture::Action::Highlight, &spy, persist));
  CHECK(spy.writes == 2);
}

TEST_CASE("selection capture policy omits Highlight for an exact existing span") {
  selection_capture::ActionBuildOptions options;
  options.annotations = true;
  options.highlight = false;
  options.removeHighlight = true;
  const auto actions = selection_capture::buildActions(options);
  REQUIRE(actions.size() == 3);
  CHECK(actions[0] == selection_capture::Action::BookNotes);
  CHECK(actions[1] == selection_capture::Action::RemoveHighlight);
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
