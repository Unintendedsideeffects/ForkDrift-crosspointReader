#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <memory>
#include <string>
#include <vector>

#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"
#include "doctest/doctest.h"

namespace {

constexpr int kFontId = 1;
constexpr char kEmDash[] = "\xe2\x80\x94";
constexpr char kHan[] = "\xe6\xbc\xa2";
constexpr char kZi[] = "\xe5\xad\x97";

BlockStyle justifiedStyle() {
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  return style;
}

std::vector<std::shared_ptr<TextBlock>> layoutWords(const std::vector<std::string>& words, const bool hyphenation,
                                                    const uint16_t width) {
  ParsedText text(/*extraParagraphSpacing=*/true, /*forceParagraphIndents=*/false, hyphenation,
                  /*focusReadingEnabled=*/false, justifiedStyle());
  for (const auto& word : words) {
    text.addWord(word, EpdFontFamily::REGULAR);
  }
  GfxRenderer renderer(display);
  std::vector<std::shared_ptr<TextBlock>> lines;
  text.layoutAndExtractLines(renderer, kFontId, width,
                             [&](std::shared_ptr<TextBlock> line) { lines.push_back(std::move(line)); });
  return lines;
}

std::string fooEmBar() { return std::string("foo") + kEmDash + "bar"; }
std::string fooEmEmBar() { return std::string("foo") + kEmDash + kEmDash + "bar"; }
std::string fooEmBarEmBaz() { return std::string("foo") + kEmDash + "bar" + kEmDash + "baz"; }
std::string hanZiEmBar() { return std::string(kHan) + kZi + kEmDash + "bar"; }
std::string fooEmHanZi() { return std::string("foo") + kEmDash + kHan + kZi; }
std::string hanZiEmEmBar() { return std::string(kHan) + kZi + kEmDash + kEmDash + "bar"; }

}  // namespace

TEST_CASE("ParsedText: hyphenation-off wraps foo—bar after the dash") {
  const auto lines = layoutWords({fooEmBar()}, false, 50);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 1);
  CHECK(lines[0]->getWords()[0] == std::string("foo") + kEmDash);
  REQUIRE(lines[1]->getWords().size() == 1);
  CHECK(lines[1]->getWords()[0] == "bar");
}

TEST_CASE("ParsedText: foo—bar on one justified line has zero gap after the dash") {
  const auto lines = layoutWords({fooEmBar()}, false, 80);
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0]->getWords().size() == 2);
  CHECK(lines[0]->getWords()[0] == std::string("foo") + kEmDash);
  CHECK(lines[0]->getWords()[1] == "bar");
  REQUIRE(lines[0]->getWordXpos().size() == 2);
  CHECK(lines[0]->getWordXpos()[1] == 40);
}

TEST_CASE("ParsedText: a multi-em-dash run stays atomic") {
  const auto lines = layoutWords({fooEmEmBar()}, false, 55);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 1);
  CHECK(lines[0]->getWords()[0] == std::string("foo") + kEmDash + kEmDash);
  REQUIRE(lines[1]->getWords().size() == 1);
  CHECK(lines[1]->getWords()[0] == "bar");
}

TEST_CASE("ParsedText: two dash sites can each wrap") {
  const auto lines = layoutWords({fooEmBarEmBaz()}, false, 50);
  REQUIRE(lines.size() == 3);
  REQUIRE(lines[0]->getWords().size() == 1);
  REQUIRE(lines[1]->getWords().size() == 1);
  REQUIRE(lines[2]->getWords().size() == 1);
  CHECK(lines[0]->getWords()[0] == std::string("foo") + kEmDash);
  CHECK(lines[1]->getWords()[0] == std::string("bar") + kEmDash);
  CHECK(lines[2]->getWords()[0] == "baz");
}

TEST_CASE("ParsedText: hyphenationEnabled still splits at tokenize time") {
  const auto lines = layoutWords({fooEmBar()}, true, 50);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 1);
  REQUIRE(lines[1]->getWords().size() == 1);
  CHECK(lines[0]->getWords()[0] == std::string("foo") + kEmDash);
  CHECK(lines[1]->getWords()[0] == "bar");
}

TEST_CASE("ParsedText: mixed CJK and Latin splits after 字 and after the dash") {
  const auto lines = layoutWords({hanZiEmBar()}, false, 35);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 2);
  CHECK(lines[0]->getWords()[0] == kHan);
  CHECK(lines[0]->getWords()[1] == std::string(kZi) + kEmDash);
  REQUIRE(lines[1]->getWords().size() == 1);
  CHECK(lines[1]->getWords()[0] == "bar");
}

TEST_CASE("ParsedText: mixed Latin and CJK splits after the dash") {
  const auto lines = layoutWords({fooEmHanZi()}, false, 45);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 1);
  CHECK(lines[0]->getWords()[0] == std::string("foo") + kEmDash);
  REQUIRE(lines[1]->getWords().size() == 2);
  CHECK(lines[1]->getWords()[0] == kHan);
  CHECK(lines[1]->getWords()[1] == kZi);
}

TEST_CASE("ParsedText: mixed CJK multi-em-dash stays atomic") {
  const auto lines = layoutWords({hanZiEmEmBar()}, false, 45);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 2);
  CHECK(lines[0]->getWords()[0] == kHan);
  CHECK(lines[0]->getWords()[1] == std::string(kZi) + kEmDash + kEmDash);
  REQUIRE(lines[1]->getWords().size() == 1);
  CHECK(lines[1]->getWords()[0] == "bar");
}

TEST_CASE("ParsedText: hyphen glue does not inflate justify gap count") {
  ParsedText text(/*extraParagraphSpacing=*/true, /*forceParagraphIndents=*/false, /*hyphenationEnabled=*/false,
                  /*focusReadingEnabled=*/false, justifiedStyle());
  text.addWord("hello", EpdFontFamily::REGULAR);
  text.addWord(fooEmBar(), EpdFontFamily::REGULAR);
  text.addWord("world", EpdFontFamily::REGULAR);
  text.addWord("ZZZZZZZZZZ", EpdFontFamily::REGULAR);

  constexpr uint16_t kWidth = 200;
  GfxRenderer renderer(display);
  std::vector<std::shared_ptr<TextBlock>> lines;
  text.layoutAndExtractLines(renderer, kFontId, kWidth,
                             [&](std::shared_ptr<TextBlock> line) { lines.push_back(std::move(line)); });

  REQUIRE(lines.size() >= 2);
  const auto& words = lines[0]->getWords();
  const auto& xpos = lines[0]->getWordXpos();
  REQUIRE(words.size() == 4);
  CHECK(words[1] == std::string("foo") + kEmDash);
  CHECK(words[2] == "bar");
  constexpr int kWordSum = 50 + 40 + 30 + 50;
  constexpr int kNaturalGaps = 6 + 6;
  constexpr size_t kGaps = 2;
  const int spare = kWidth - kWordSum - kNaturalGaps;
  const int extra = spare / static_cast<int>(kGaps);
  REQUIRE(xpos.size() == 4);
  CHECK(xpos[1] == static_cast<int16_t>(50 + 6 + extra));
  CHECK(xpos[2] == static_cast<int16_t>(xpos[1] + 40));
}
