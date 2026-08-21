#include "Epub/blocks/BlockStyle.h"
#include "doctest/doctest.h"

// Regression coverage for the unified <br> handling ported from upstream
// 44ff31374 (standalone <br> as a visible section break) and b508f75e7 (CJK
// <br> margin semantics), and for the adversarial-review fixes to categories
// (3) leading/repeated <br> flattening (plans/REVIEW-epubparse.md). The SAX
// orchestration lives in ChapterHtmlSlimParser::startNewTextBlock(), which is
// not link-reachable in a host test without the full Epub/image-decoding
// stack (see the SUMMARY and docs/FINDINGS.md). The actual gap-injection
// *policy* that logic delegates to, BlockStyle::mergeEmptyBlockOnBrGap(), is
// reachable and header-only, so these tests call that real production
// function directly rather than re-deriving its logic inline.

namespace {
constexpr int16_t kLineHeightPx = 20;  // mirrors renderer.getLineHeight() in production
}  // namespace

TEST_CASE("BlockStyle: fromBrElement never survives a combine, so it cannot leak to a sibling") {
  BlockStyle brBlock;
  brBlock.fromBrElement = true;
  brBlock.marginTop = 4;

  BlockStyle nextSibling;
  nextSibling.marginTop = 2;

  const auto combined = brBlock.getCombinedBlockStyle(nextSibling, BlockStyle::CombineAxis::Vertical);
  CHECK_FALSE(combined.fromBrElement);
}

TEST_CASE("BlockStyle: a <br> left empty when the next block opens gets a full line-height gap") {
  BlockStyle emptyBrBlockStyle;
  emptyBrBlockStyle.marginTop = 4;  // whatever margin the container already deposited

  BlockStyle incoming;
  incoming.marginTop = 2;  // the next block's own (small) margin-top
  incoming.fromBrElement = true;

  const auto combined = emptyBrBlockStyle.mergeEmptyBlockOnBrGap(incoming, kLineHeightPx);
  CHECK(combined.marginTop >= kLineHeightPx);
}

TEST_CASE("BlockStyle: without the fromBrElement tag, no line-height gap is injected") {
  // Contrast case: an ordinary empty block (not a <br>) opening a sibling
  // must not silently gain a blank line -- only the tagged <br> path does.
  BlockStyle plainEmptyBlockStyle;
  plainEmptyBlockStyle.marginTop = 4;

  BlockStyle incoming;
  incoming.marginTop = 2;
  // incoming.fromBrElement left false: this is an ordinary sibling block, not a <br>.

  const auto combined = plainEmptyBlockStyle.mergeEmptyBlockOnBrGap(incoming, kLineHeightPx);
  CHECK(combined.marginTop == 4);  // max(4, 2), no line-height added
  CHECK(combined.marginTop < kLineHeightPx);
}

TEST_CASE("BlockStyle: a <br> that is the first child of a block still gets a line-height gap") {
  // Review category (3), "zero spacing" finding: <p><br>Text</p>. The block <p> creates
  // is empty and was never itself tagged fromBrElement (it was opened by <p>, not a
  // <br>), so a fix that reads the *stored* block's flag (as the pre-fix code did) never
  // injects here. Reading the incoming <br> event's own flag does.
  BlockStyle freshParagraphBlockStyle;  // what <p> deposits on open; never br-tagged
  freshParagraphBlockStyle.marginTop = 0;

  BlockStyle firstBr;
  firstBr.marginTop = 0;
  firstBr.fromBrElement = true;

  const auto combined = freshParagraphBlockStyle.mergeEmptyBlockOnBrGap(firstBr, kLineHeightPx);
  CHECK(combined.marginTop == kLineHeightPx);
}

TEST_CASE("BlockStyle: each consecutive <br> on a still-empty block adds its own line-height gap") {
  // Review category (3), "degenerate flattening" finding: <br><br><br> must not read the
  // same as <br><br>. Simulates ChapterHtmlSlimParser's actual call pattern -- one
  // mergeEmptyBlockOnBrGap() call per <br>, each re-tagging fromBrElement=true fresh,
  // folded into the block's running style from the previous call.
  BlockStyle style;  // the block's running style, updated after each simulated <br>
  style.marginTop = 0;

  BlockStyle brEvent;
  brEvent.marginTop = 0;
  brEvent.fromBrElement = true;

  style = style.mergeEmptyBlockOnBrGap(brEvent, kLineHeightPx);
  CHECK(style.marginTop == 1 * kLineHeightPx);

  style = style.mergeEmptyBlockOnBrGap(brEvent, kLineHeightPx);
  CHECK(style.marginTop == 2 * kLineHeightPx);  // the bug: this used to stay at 1x

  style = style.mergeEmptyBlockOnBrGap(brEvent, kLineHeightPx);
  CHECK(style.marginTop == 3 * kLineHeightPx);  // and this too
}

TEST_CASE("BlockStyle: an inline <br> after text strips container margins, not other fields") {
  // Mirrors ChapterHtmlSlimParser.cpp:1431-1436: a <br> that follows real
  // text on the current block is a plain line break, so it must not re-add
  // the container's paragraph spacing -- but it also must not lose unrelated
  // style (horizontal margins, alignment) in the process.
  BlockStyle brStyle;
  brStyle.marginTop = 10;
  brStyle.marginBottom = 8;
  brStyle.marginLeft = 12;
  brStyle.alignment = CssTextAlign::Center;

  const auto stripped = brStyle.withoutTop().withoutBottom();
  CHECK(stripped.marginTop == 0);
  CHECK(stripped.marginBottom == 0);
  CHECK(stripped.marginLeft == 12);
  CHECK(stripped.alignment == CssTextAlign::Center);
}
