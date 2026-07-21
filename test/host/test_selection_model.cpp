#include <vector>

#include "activities/reader/SelectionModel.h"
#include "doctest/doctest.h"

namespace {

std::vector<selection::SelWord> makeWords(const std::vector<const char*>& texts) {
  std::vector<selection::SelWord> words;
  words.reserve(texts.size());
  for (const char* text : texts) {
    words.push_back({0, 0, 0, 0, 0, text});
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

TEST_CASE("SelectionModel joinSpanFormatted preserves line breaks") {
  std::vector<selection::SelWord> words;
  words.push_back({0, 0, 0, 0, 0, "line"});
  words.push_back({0, 0, 0, 0, 0, "one"});
  words.push_back({0, 0, 0, 0, 1, "line"});
  words.push_back({0, 0, 0, 0, 1, "two"});

  CHECK(selection::joinSpanFormatted(words, 0, 3) == "line one\nline two");
}

TEST_CASE("SelectionModel joinSpan preserves selection spacing") {
  const auto words = makeWords({"one", "two", "three"});

  CHECK(selection::joinSpan(words, 0, 2) == "one two three");
  CHECK(selection::joinSpan(words, 1, 5) == "two three");
}

TEST_CASE("SelectionModel sentenceSpan expands a single-word selection to its sentence") {
  const auto words = makeWords({"The", "quick", "brown", "fox.", "It", "ran", "away."});

  // Select "brown" (index 2) -> whole first sentence.
  CHECK(selection::sentenceSpan(words, 2, 2) == "The quick brown fox.");
  // Select "ran" (index 5) -> second sentence only.
  CHECK(selection::sentenceSpan(words, 5, 5) == "It ran away.");
}

TEST_CASE("SelectionModel sentenceSpan handles page edges without a terminator") {
  const auto words = makeWords({"a", "mid-page", "fragment", "with", "no", "period"});

  CHECK(selection::sentenceSpan(words, 2, 2) == "a mid-page fragment with no period");
}

TEST_CASE("SelectionModel sentenceSpan ignores terminators hidden behind closing quotes") {
  const auto words = makeWords({"He", "said", "\"stop!\"", "and", "left."});

  // The '!' is a real terminator even with a trailing quote, so it ends here.
  CHECK(selection::sentenceSpan(words, 0, 0) == "He said \"stop!\"");
  CHECK(selection::sentenceSpan(words, 3, 3) == "and left.");
}

TEST_CASE("SelectionModel sentenceSpan trims to a word boundary under the cap") {
  const auto words = makeWords({"alpha", "beta", "gamma", "delta"});

  // Cap of 12 fits "alpha beta" (10) but not "... gamma" (16); trim on the space.
  CHECK(selection::sentenceSpan(words, 0, 0, 12) == "alpha beta");
}

TEST_CASE("SelectionModel sentenceSpan retains a late selection under the cap") {
  const auto words = makeWords({"alpha", "beta", "gamma", "delta"});

  CHECK(selection::sentenceSpan(words, 3, 3, 12) == "gamma delta");
}

TEST_CASE("SelectionModel sentenceSpan does not split unspaced UTF-8 selections") {
  const auto words = makeWords({"ééé"});

  // Five bytes would split the third two-byte codepoint, so the soft cap yields
  // to retaining the complete selected word.
  CHECK(selection::sentenceSpan(words, 0, 0, 5) == "ééé");
}

TEST_CASE("SelectionModel sentenceSpan is safe on an empty page") {
  const std::vector<selection::SelWord> words;

  CHECK(selection::sentenceSpan(words, 0, 0) == "");
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
