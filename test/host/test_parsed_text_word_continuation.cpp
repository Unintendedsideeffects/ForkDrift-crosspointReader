#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <memory>
#include <vector>

#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"
#include "doctest/doctest.h"

// Regression coverage for upstream 3319aa172 (long-word/CJK continuation).
//
// ChapterHtmlSlimParser::characterData() buffers characters into a fixed
// partWordBuffer[MAX_WORD_SIZE + 1] (MAX_WORD_SIZE = 200,
// ChapterHtmlSlimParser.h:22). When a single unbroken run of text (a long URL,
// or CJK prose with no spaces) exceeds that, the buffer's overflow branch
// (ChapterHtmlSlimParser.cpp:1740) flushes the first 200 bytes as one
// ParsedText word and starts accumulating the rest as a second call to
// addWord(). Before this change, that second call passed
// attachToPrevious=false, so the two fragments became two ordinary,
// independently-breakable words with a synthetic space between them --
// visually splitting one unbroken run in the middle. The fix passes
// attachToPrevious=true (ChapterHtmlSlimParser.cpp:1755,1762) so the fragments
// are laid out as a single continuous unit.
//
// This exercises that exact contract at the ParsedText level (the shared
// underlying data structure ChapterHtmlSlimParser feeds), since
// ChapterHtmlSlimParser itself is not link-reachable in a host test without
// the full Epub/image-decoding stack (out of scope for this slice; see the
// SUMMARY and docs/FINDINGS.md).
//
// The two fragment widths and pageWidth below are chosen so the two ways of
// joining them produce different fit outcomes:
//   - joined by continuation (kerning-only gap, stubbed to 0): 100+0+100=200
//   - joined as ordinary words (space gap, stubbed to 6):      100+6+100=206
// pageWidth=203 sits strictly between them, so only the continuation case can
// fit both fragments on one line.

namespace {

constexpr int kFontId = 1;
constexpr uint16_t kPageWidthFitsOnlyWithContinuation = 203;

// Word-continuation fragments as ChapterHtmlSlimParser::characterData() would
// produce them: pure ASCII so getTextAdvanceX's per-codepoint stub is exact.
const std::string kFragmentA(10, 'A');  // 100px at 10px/char
const std::string kFragmentB(10, 'B');  // 100px at 10px/char

BlockStyle nonJustifiedLeftAlignedStyle() {
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  return style;
}

std::vector<std::shared_ptr<TextBlock>> layoutFragments(bool secondFragmentContinues) {
  // extraParagraphSpacing=true skips applyParagraphIndent()'s EmSpace
  // insertion, and hyphenationEnabled=false keeps computeLineBreaks() (not
  // the hyphenating variant) in play regardless of the ENABLE_HYPHENATION
  // build flag -- both are irrelevant to the invariant under test and would
  // otherwise perturb the exact pixel arithmetic above.
  ParsedText text(/*extraParagraphSpacing=*/true, /*forceParagraphIndents=*/false,
                  /*hyphenationEnabled=*/false, /*focusReadingEnabled=*/false, nonJustifiedLeftAlignedStyle());
  text.addWord(kFragmentA, EpdFontFamily::REGULAR, false, /*attachToPrevious=*/false);
  text.addWord(kFragmentB, EpdFontFamily::REGULAR, false, /*attachToPrevious=*/secondFragmentContinues);

  GfxRenderer renderer(display);
  std::vector<std::shared_ptr<TextBlock>> lines;
  text.layoutAndExtractLines(renderer, kFontId, kPageWidthFitsOnlyWithContinuation,
                              [&](std::shared_ptr<TextBlock> line) { lines.push_back(std::move(line)); });
  return lines;
}

}  // namespace

TEST_CASE("ParsedText: a continuation-flagged word-overflow fragment rejoins as one unbroken run") {
  const auto lines = layoutFragments(/*secondFragmentContinues=*/true);

  // The DP line-breaker refuses to end a line before a continuation word
  // (ParsedText.cpp:648-651), so both fragments must land in the same
  // TextBlock even though pageWidth alone would not fit them as two
  // ordinarily-spaced words.
  REQUIRE(lines.size() == 1);
  const auto& words = lines[0]->getWords();
  REQUIRE(words.size() == 2);
  CHECK(words[0] == kFragmentA);
  CHECK(words[1] == kFragmentB);

  // Zero-gap adjacency: the continuation path uses getKerning() (stubbed 0),
  // not getSpaceAdvance() (stubbed 6), so the second fragment starts exactly
  // where the first ends -- no synthetic space in the middle of the word.
  const auto& xpos = lines[0]->getWordXpos();
  REQUIRE(xpos.size() == 2);
  CHECK(xpos[1] == xpos[0] + static_cast<int16_t>(kFragmentA.size() * 10));
}

TEST_CASE("ParsedText: without the continuation flag, an overflow fragment is a separate breakable word") {
  // Contrast case proving the test above actually discriminates: this is the
  // pre-3319aa172-port behavior. The DP line-breaker is now free to end a
  // line at fragment A alone (no continuation constraint), and since A+space
  // (stubbed 6px) +B totals 206px > the 203px page width, it must.
  const auto lines = layoutFragments(/*secondFragmentContinues=*/false);

  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0]->getWords().size() == 1);
  CHECK(lines[0]->getWords()[0] == kFragmentA);
  REQUIRE(lines[1]->getWords().size() == 1);
  CHECK(lines[1]->getWords()[0] == kFragmentB);
}

TEST_CASE("ParsedText: a fragment at exactly MAX_WORD_SIZE (200) bytes is unaffected by the fix") {
  // Sanity check on the claim in the SUMMARY: the parser's overflow buffer
  // (ChapterHtmlSlimParser.h:22,43) bounds every single ParsedText word to
  // <=200 bytes both before and after this change -- the fix only changes
  // whether *consecutive* fragments attach, never the per-fragment cap.
  const std::string maxSizeFragment(200, 'x');
  ParsedText text(/*extraParagraphSpacing=*/true, /*forceParagraphIndents=*/false,
                  /*hyphenationEnabled=*/false, /*focusReadingEnabled=*/false, nonJustifiedLeftAlignedStyle());
  text.addWord(maxSizeFragment, EpdFontFamily::REGULAR, false, false);
  CHECK(text.size() == 1);
}

// Regression coverage for the sibling fix to 552b2683e's bullet-reuse branch:
// ChapterHtmlSlimParser::startNewTextBlock() used to gate the "reuse this
// block for a nested <li><p>...</p></li>" branch on currentTextBlock->isEmpty(),
// but the <li> handler had already called addWord() to deposit the bullet
// glyph before that check ever runs (ChapterHtmlSlimParser.cpp:1452-1454) --
// making the reuse branch permanently unreachable. This pins down the
// ParsedText-level precondition the fix (checking listItemBulletOnly instead
// of isEmpty()) depends on.
TEST_CASE("ParsedText: isEmpty() is false immediately after the li bullet glyph is added") {
  ParsedText text(/*extraParagraphSpacing=*/true, /*forceParagraphIndents=*/false,
                  /*hyphenationEnabled=*/false, /*focusReadingEnabled=*/false, nonJustifiedLeftAlignedStyle());
  CHECK(text.isEmpty());
  text.addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR);  // the bullet glyph <li> deposits
  CHECK_FALSE(text.isEmpty());
}
