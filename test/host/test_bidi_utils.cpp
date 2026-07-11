#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "lib/MiniBidi/BidiUtils.h"

TEST_CASE("BidiUtils computeVisualWordOrder: Pure LTR, LTR paragraph") {
  std::vector<std::string> words = {"one", "two", "three"};
  std::vector<uint16_t> visualOrder;
  bool reordered = BidiUtils::computeVisualWordOrder(words, false, visualOrder);
  CHECK(reordered == false);
  CHECK(visualOrder.empty());
}

TEST_CASE("BidiUtils computeVisualWordOrder: Pure RTL, RTL paragraph") {
  // א, ב, ג
  std::vector<std::string> words = {"\xD7\x90", "\xD7\x91", "\xD7\x92"};
  std::vector<uint16_t> visualOrder;
  bool reordered = BidiUtils::computeVisualWordOrder(words, true, visualOrder);
  CHECK(reordered == true);
  REQUIRE(visualOrder.size() == 3);
  CHECK(visualOrder[0] == 2);
  CHECK(visualOrder[1] == 1);
  CHECK(visualOrder[2] == 0);
}

TEST_CASE("BidiUtils computeVisualWordOrder: Pure LTR line, RTL paragraph") {
  std::vector<std::string> words = {"one", "two"};
  std::vector<uint16_t> visualOrder;
  bool reordered = BidiUtils::computeVisualWordOrder(words, true, visualOrder);
  CHECK(reordered == true);
  REQUIRE(visualOrder.size() == 2);
  CHECK(visualOrder[0] == 0);
  CHECK(visualOrder[1] == 1);
}

TEST_CASE("BidiUtils computeVisualWordOrder: Mixed line, RTL paragraph") {
  // {א, "abc", ג}
  std::vector<std::string> words = {"\xD7\x90", "abc", "\xD7\x92"};
  std::vector<uint16_t> visualOrder;
  bool reordered = BidiUtils::computeVisualWordOrder(words, true, visualOrder);
  CHECK(reordered == true);
  REQUIRE(visualOrder.size() == 3);
  CHECK(visualOrder[0] == 2);
  CHECK(visualOrder[1] == 1);
  CHECK(visualOrder[2] == 0);
}

TEST_CASE("BidiUtils computeVisualWordOrder: Numbers in RTL") {
  // {א, "123", ג}
  std::vector<std::string> words = {"\xD7\x90", "123", "\xD7\x92"};
  std::vector<uint16_t> visualOrder;
  bool reordered = BidiUtils::computeVisualWordOrder(words, true, visualOrder);
  CHECK(reordered == true);
  REQUIRE(visualOrder.size() == 3);
  CHECK(visualOrder[0] == 2);
  CHECK(visualOrder[1] == 1);
  CHECK(visualOrder[2] == 0);
}

TEST_CASE("BidiUtils computeVisualWordOrder: Oversize line") {
  std::vector<std::string> words(130, "a");
  std::vector<uint16_t> visualOrder;
  bool reordered = BidiUtils::computeVisualWordOrder(words, false, visualOrder);
  CHECK(reordered == false);
  CHECK(visualOrder.empty());
}

TEST_CASE("BidiUtils startsWithRtl and detectParagraphLevel") {
  // Hebrew א = \xD7\x90
  const char* hebrewFirst =
      "\xD7\x90"
      "abc";
  const char* englishFirst =
      "abc"
      "\xD7\x90";
  const char* neutralOnly = "... ";

  CHECK(BidiUtils::startsWithRtl(hebrewFirst) == true);
  CHECK(BidiUtils::startsWithRtl(englishFirst) == false);
  CHECK(BidiUtils::startsWithRtl(neutralOnly) == false);

  CHECK(BidiUtils::detectParagraphLevel(hebrewFirst, 0) == 1);
  CHECK(BidiUtils::detectParagraphLevel(englishFirst, 1) == 0);
  CHECK(BidiUtils::detectParagraphLevel(neutralOnly, 1) == 1);
  CHECK(BidiUtils::detectParagraphLevel(neutralOnly, 0) == 0);
}
