#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <memory>
#include <vector>

#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"
#include "doctest/doctest.h"

namespace {

BlockStyle leftStyleWithFont(int fontId) {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.fontId = fontId;
  return style;
}

std::shared_ptr<TextBlock> layoutOneLine(int layoutFontId, int storedFontId) {
  ParsedText text(/*extraParagraphSpacing=*/true, /*forceParagraphIndents=*/false,
                  /*hyphenationEnabled=*/false, /*focusReadingEnabled=*/false, leftStyleWithFont(storedFontId));
  text.addWord("AAA", EpdFontFamily::REGULAR);
  text.addWord("BBB", EpdFontFamily::REGULAR);
  GfxRenderer renderer(display);
  std::vector<std::shared_ptr<TextBlock>> lines;
  text.layoutAndExtractLines(renderer, layoutFontId, 400, [&](std::shared_ptr<TextBlock> line, uint32_t /*offset*/) {
    lines.push_back(std::move(line));
  });
  REQUIRE(lines.size() == 1);
  return lines[0];
}

}  // namespace

TEST_CASE("ParsedText: a heading fontId measures wider than the body font") {
  const auto body = layoutOneLine(1, 0);
  const auto heading = layoutOneLine(2, 2);
  REQUIRE(body->getWordXpos().size() == 2);
  REQUIRE(heading->getWordXpos().size() == 2);
  CHECK(heading->getWordXpos()[1] > body->getWordXpos()[1]);
  CHECK(heading->getBlockStyle().fontId == 2);
  CHECK(body->getBlockStyle().fontId == 0);
}
