#include <vector>

#include "activities/reader/SelectionModel.h"
#include "doctest/doctest.h"

namespace {

std::vector<selection::SelWord> makeWords(const std::vector<const char*>& texts) {
  std::vector<selection::SelWord> words;
  words.reserve(texts.size());
  for (const char* text : texts) {
    words.push_back({0, 0, 0, 0, text});
  }
  return words;
}

selection::Model makeModel(const std::vector<const char*>& texts) {
  selection::Model model;
  model.words = makeWords(texts);
  return model;
}

}  // namespace

TEST_CASE("SelectionModel move clamps and drags anchor before anchoring") {
  auto model = makeModel({"one", "two", "three"});

  selection::move(model, -1);
  CHECK(model.cursor == 0);
  CHECK(model.anchor == 0);

  selection::move(model, 1);
  CHECK(model.cursor == 1);
  CHECK(model.anchor == 1);

  selection::move(model, 8);
  CHECK(model.cursor == 2);
  CHECK(model.anchor == 2);
}

TEST_CASE("SelectionModel anchored move extends span in both directions") {
  auto model = makeModel({"one", "two", "three", "four"});
  model.cursor = 1;
  model.anchor = 1;
  model.anchored = true;

  selection::move(model, 2);
  CHECK(model.cursor == 3);
  CHECK(model.anchor == 1);
  CHECK(selection::span(model) == std::pair<int, int>{1, 3});

  selection::move(model, -3);
  CHECK(model.cursor == 0);
  CHECK(model.anchor == 1);
  CHECK(selection::span(model) == std::pair<int, int>{0, 1});
}

TEST_CASE("SelectionModel paging clamps by eight words") {
  auto model = makeModel({"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"});

  selection::move(model, 8);
  CHECK(model.cursor == 8);
  CHECK(model.anchor == 8);

  selection::move(model, 8);
  CHECK(model.cursor == 9);
  CHECK(model.anchor == 9);

  selection::move(model, -8);
  CHECK(model.cursor == 1);
  CHECK(model.anchor == 1);

  selection::move(model, -8);
  CHECK(model.cursor == 0);
  CHECK(model.anchor == 0);
}

TEST_CASE("SelectionModel stepBack collapses anchored selections and signals exit otherwise") {
  auto model = makeModel({"one", "two", "three"});
  model.anchor = 1;
  model.cursor = 2;
  model.anchored = true;

  CHECK(selection::stepBack(model));
  CHECK_FALSE(model.anchored);
  CHECK(model.cursor == 1);
  CHECK(model.anchor == 1);

  CHECK_FALSE(selection::stepBack(model));
}

TEST_CASE("SelectionModel joinSpan preserves selection spacing") {
  const auto words = makeWords({"one", "two", "three"});

  CHECK(selection::joinSpan(words, 0, 2) == "one two three");
  CHECK(selection::joinSpan(words, 1, 5) == "two three");
}

TEST_CASE("SelectionModel anchorByText uses hints then slides by first word") {
  const auto words = makeWords({"alpha", "beta", "gamma", "alpha", "beta"});

  int lo = -1;
  int hi = -1;
  CHECK(selection::anchorByText(words, "beta gamma", 1, 2, lo, hi));
  CHECK(lo == 1);
  CHECK(hi == 2);

  lo = -1;
  hi = -1;
  CHECK(selection::anchorByText(words, "alpha beta", 1, 2, lo, hi));
  CHECK(lo == 0);
  CHECK(hi == 1);

  lo = -1;
  hi = -1;
  CHECK_FALSE(selection::anchorByText(words, "missing text", 0, 1, lo, hi));
  CHECK(lo == -1);
  CHECK(hi == -1);
}

TEST_CASE("SelectionModel anchorByTextUnique finds a unique match") {
  const auto words = makeWords({"alpha", "beta", "gamma"});

  int lo = -1;
  int hi = -1;
  bool unique = false;
  CHECK(selection::anchorByTextUnique(words, "beta gamma", 1, lo, hi, unique));
  CHECK(unique);
  CHECK(lo == 1);
  CHECK(hi == 2);
}

TEST_CASE("SelectionModel anchorByTextUnique reports duplicate matches") {
  const auto words = makeWords({"the", "cat", "sat", "the", "cat"});

  int lo = -1;
  int hi = -1;
  bool unique = true;
  CHECK(selection::anchorByTextUnique(words, "the cat", 1, lo, hi, unique));
  CHECK_FALSE(unique);
  CHECK(lo == 0);
  CHECK(hi == 1);
}

TEST_CASE("SelectionModel anchorByTextUnique reports no match") {
  const auto words = makeWords({"alpha", "beta", "gamma"});

  int lo = -1;
  int hi = -1;
  bool unique = true;
  CHECK_FALSE(selection::anchorByTextUnique(words, "missing text", 1, lo, hi, unique));
  CHECK_FALSE(unique);
}

TEST_CASE("SelectionModel anchorByTextUnique detects duplicate single-word matches") {
  const auto words = makeWords({"the", "cat", "the"});

  int lo = -1;
  int hi = -1;
  bool unique = true;
  CHECK(selection::anchorByTextUnique(words, "the", 0, lo, hi, unique));
  CHECK_FALSE(unique);
  CHECK(lo == 0);
  CHECK(hi == 0);
}

TEST_CASE("SelectionModel anchorByTextUnique matches at the end of the page") {
  const auto words = makeWords({"zero", "one", "two"});

  int lo = -1;
  int hi = -1;
  bool unique = false;
  CHECK(selection::anchorByTextUnique(words, "one two", 1, lo, hi, unique));
  CHECK(unique);
  CHECK(lo == 1);
  CHECK(hi == 2);
}
