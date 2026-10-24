#include "Epub/blocks/BlockStyle.h"
#include "doctest/doctest.h"

namespace {

CssStyle styleWithFontSize(float value, CssUnit unit) {
  CssStyle style;
  style.fontSize = CssLength{value, unit};
  style.defined.fontSize = 1;
  return style;
}

constexpr float kEmSize = 16.0f;
constexpr uint16_t kViewportWidth = 600;

}  // namespace

TEST_CASE("BlockStyle: fromCssStyle copies forced page breaks and leaves unset as false") {
  CssStyle withBreaks;
  withBreaks.pageBreakBefore = true;
  withBreaks.defined.pageBreakBefore = 1;
  withBreaks.pageBreakAfter = true;
  withBreaks.defined.pageBreakAfter = 1;
  const auto resolved = BlockStyle::fromCssStyle(withBreaks, kEmSize, CssTextAlign::Left, kViewportWidth);
  CHECK(resolved.pageBreakBefore);
  CHECK(resolved.pageBreakAfter);

  CssStyle empty;
  const auto none = BlockStyle::fromCssStyle(empty, kEmSize, CssTextAlign::Left, kViewportWidth);
  CHECK_FALSE(none.pageBreakBefore);
  CHECK_FALSE(none.pageBreakAfter);
}

TEST_CASE("BlockStyle: fromCssStyle ignores page-break defined as auto/avoid (flag true, value false)") {
  CssStyle avoid;
  avoid.pageBreakBefore = false;
  avoid.defined.pageBreakBefore = 1;
  const auto resolved = BlockStyle::fromCssStyle(avoid, kEmSize, CssTextAlign::Left, kViewportWidth);
  CHECK_FALSE(resolved.pageBreakBefore);
}

TEST_CASE("BlockStyle: vertical combine ORs page-break-before onto the first child") {
  BlockStyle parent;
  parent.pageBreakBefore = true;
  BlockStyle child;
  const auto combined = parent.getCombinedBlockStyle(child, BlockStyle::CombineAxis::Vertical);
  CHECK(combined.pageBreakBefore);

  BlockStyle already;
  already.pageBreakBefore = true;
  CHECK(parent.getCombinedBlockStyle(already, BlockStyle::CombineAxis::Vertical).pageBreakBefore);
}

TEST_CASE("BlockStyle: horizontal combine does not inherit page-break-before") {
  BlockStyle parent;
  parent.pageBreakBefore = true;
  BlockStyle child;
  const auto combined = parent.getCombinedBlockStyle(child, BlockStyle::CombineAxis::Horizontal);
  CHECK_FALSE(combined.pageBreakBefore);
}

TEST_CASE("BlockStyle: fontScale inherits when the child does not define it") {
  BlockStyle parent;
  parent.fontScale = 1.5f;
  parent.fontScaleDefined = true;
  parent.fontId = 99;
  BlockStyle child;
  const auto combined = parent.getCombinedBlockStyle(child, BlockStyle::CombineAxis::Horizontal);
  CHECK(combined.fontScaleDefined);
  CHECK(combined.fontScale == doctest::Approx(1.5f));
  CHECK(combined.fontId == 99);
}

TEST_CASE("BlockStyle: a child's defined fontScale wins over the parent") {
  BlockStyle parent;
  parent.fontScale = 1.5f;
  parent.fontScaleDefined = true;
  parent.fontId = 99;
  BlockStyle child;
  child.fontScale = 2.0f;
  child.fontScaleDefined = true;
  child.fontId = 7;
  const auto combined = parent.getCombinedBlockStyle(child, BlockStyle::CombineAxis::Horizontal);
  CHECK(combined.fontScale == doctest::Approx(2.0f));
  CHECK(combined.fontId == 7);
}

TEST_CASE("BlockStyle: keepWithNext and pageBreakAfter are not inherited") {
  BlockStyle parent;
  parent.keepWithNext = true;
  parent.pageBreakAfter = true;
  BlockStyle child;
  const auto combined = parent.getCombinedBlockStyle(child, BlockStyle::CombineAxis::Vertical);
  CHECK_FALSE(combined.keepWithNext);
  CHECK_FALSE(combined.pageBreakAfter);
}

TEST_CASE("BlockStyle: fromBrElement still does not survive a combine") {
  BlockStyle brBlock;
  brBlock.fromBrElement = true;
  brBlock.fontScale = 1.2f;
  brBlock.fontScaleDefined = true;
  BlockStyle next;
  const auto combined = brBlock.getCombinedBlockStyle(next, BlockStyle::CombineAxis::Vertical);
  CHECK_FALSE(combined.fromBrElement);
}

TEST_CASE("BlockStyle: resolvedFontId falls back to the section font when unset") {
  BlockStyle style;
  CHECK(style.resolvedFontId(12) == 12);
  style.fontId = 18;
  CHECK(style.resolvedFontId(12) == 18);
}

TEST_CASE("BlockStyle: fromCssStyle does not invent a fontScale") {
  const auto resolved =
      BlockStyle::fromCssStyle(styleWithFontSize(2.0f, CssUnit::Em), kEmSize, CssTextAlign::Left, kViewportWidth);
  CHECK_FALSE(resolved.fontScaleDefined);
  CHECK(resolved.fontId == 0);
}
