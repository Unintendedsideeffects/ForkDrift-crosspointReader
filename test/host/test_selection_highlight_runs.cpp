#include <vector>

#include "activities/reader/SelectionModel.h"
#include "doctest/doctest.h"

namespace {

selection::SelWord word(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t lineId, const char* text) {
  return {x, y, w, h, lineId, text};
}

}  // namespace

TEST_CASE("buildHighlightRuns merges same-line gaps") {
  const std::vector<selection::SelWord> words = {
      word(10, 20, 30, 12, 0, "hello"),
      word(50, 20, 25, 12, 0, "world"),
  };

  const auto runs = selection::buildHighlightRuns(words, 0, 1);
  REQUIRE(runs.size() == 1);
  CHECK(runs[0].x == 9);
  CHECK(runs[0].y == 20);
  CHECK(runs[0].w == 67);
  CHECK(runs[0].h == 12);
}

TEST_CASE("buildHighlightRuns keeps separate lines") {
  const std::vector<selection::SelWord> words = {
      word(10, 20, 30, 12, 0, "one"),
      word(10, 40, 30, 12, 1, "two"),
      word(50, 40, 20, 12, 1, "three"),
  };

  const auto runs = selection::buildHighlightRuns(words, 0, 2);
  REQUIRE(runs.size() == 2);
  CHECK(runs[0].w == 32);
  CHECK(runs[1].w == 62);
}

TEST_CASE("buildHighlightRuns handles RTL coordinate order") {
  const std::vector<selection::SelWord> words = {
      word(80, 20, 20, 12, 0, "world"),
      word(40, 20, 25, 12, 0, "hello"),
  };

  const auto runs = selection::buildHighlightRuns(words, 0, 1);
  REQUIRE(runs.size() == 1);
  CHECK(runs[0].x == 39);
  CHECK(runs[0].w == 62);
}

TEST_CASE("buildHighlightRuns handles reversed bounds and adjacent line ids") {
  const std::vector<selection::SelWord> words = {
      word(10, 20, 20, 12, 0, "a"),
      word(40, 20, 20, 12, 1, "b"),
  };

  CHECK(selection::buildHighlightRuns(words, 1, 0).empty());
  const auto runs = selection::buildHighlightRuns(words, 0, 0);
  REQUIRE(runs.size() == 1);
  CHECK(runs[0].w == 22);
}
