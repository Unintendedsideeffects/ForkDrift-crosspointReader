#include "Epub/blocks/BlockStyle.h"
#include "doctest/doctest.h"

// Regression coverage for the unified <br> handling ported from upstream
// 44ff31374 (standalone <br> as a visible section break) and b508f75e7 (CJK
// <br> margin semantics). The actual gap injection lives in
// ChapterHtmlSlimParser::startNewTextBlock() (ChapterHtmlSlimParser.cpp:294-
// 303), which is not link-reachable in a host test without the full
// Epub/image-decoding stack (see the SUMMARY). These tests instead pin down
// the BlockStyle-level building blocks that logic is built from -- the parts
// that are reachable and where the actual margin-merge policy lives.

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
  // Mirrors ChapterHtmlSlimParser.cpp:298-303: the currently-empty block's
  // style is tagged fromBrElement (a standalone <br>, or the last of several
  // consecutive <br>s), so the next block's incoming margin gets a full
  // line height added before the two styles are merged.
  BlockStyle emptyBrBlockStyle;
  emptyBrBlockStyle.fromBrElement = true;
  emptyBrBlockStyle.marginTop = 4;  // whatever margin the container already deposited

  BlockStyle incoming;
  incoming.marginTop = 2;  // the next block's own (small) margin-top
  if (emptyBrBlockStyle.fromBrElement) {
    incoming.marginTop = static_cast<int16_t>(incoming.marginTop + kLineHeightPx);
  }

  const auto combined = emptyBrBlockStyle.getCombinedBlockStyle(incoming, BlockStyle::CombineAxis::Vertical);
  // Vertical combine takes max(child, parent) margins (BlockStyle.h:97), so the
  // line-height gap survives only because it was folded into incoming (the
  // child) first -- this pins that ordering down.
  CHECK(combined.marginTop >= kLineHeightPx);
}

TEST_CASE("BlockStyle: without the fromBrElement tag, no line-height gap is injected") {
  // Contrast case: an ordinary empty block (not a <br>) opening a sibling
  // must not silently gain a blank line -- only the tagged <br> path does.
  BlockStyle plainEmptyBlockStyle;
  plainEmptyBlockStyle.marginTop = 4;

  BlockStyle incoming;
  incoming.marginTop = 2;
  if (plainEmptyBlockStyle.fromBrElement) {
    incoming.marginTop = static_cast<int16_t>(incoming.marginTop + kLineHeightPx);
  }

  const auto combined = plainEmptyBlockStyle.getCombinedBlockStyle(incoming, BlockStyle::CombineAxis::Vertical);
  CHECK(combined.marginTop == 4);  // max(4, 2), no line-height added
  CHECK(combined.marginTop < kLineHeightPx);
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
