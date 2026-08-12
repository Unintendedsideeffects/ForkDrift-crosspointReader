#include "Epub/blocks/BlockStyle.h"
#include "doctest/doctest.h"

// BlockStyle::fromCssStyle owns the alignment precedence policy:
//   - an explicit user setting beats embedded text-align
//   - CssTextAlign::None means "Book's Style", deferring to the CSS
// ChapterHtmlSlimParser used to pre-resolve the CSS value and pass it in as if
// the user had chosen it, which inverted the first rule. These cases pin the
// policy down so a caller cannot quietly defeat it again.

namespace {

CssStyle styleWithAlign(CssTextAlign align) {
  CssStyle style;
  style.textAlign = align;
  style.defined.textAlign = 1;
  return style;
}

constexpr float kEmSize = 16.0f;
constexpr uint16_t kViewportWidth = 600;

}  // namespace

TEST_CASE("BlockStyle: an explicit user alignment overrides embedded text-align") {
  // The regression: a book styling its paragraphs as centred used to win over a
  // reader who had explicitly asked for left alignment.
  const auto style =
      BlockStyle::fromCssStyle(styleWithAlign(CssTextAlign::Center), kEmSize, CssTextAlign::Left, kViewportWidth);
  CHECK(style.alignment == CssTextAlign::Left);
}

TEST_CASE("BlockStyle: an explicit user alignment applies when the CSS defines none") {
  CssStyle style;
  const auto resolved = BlockStyle::fromCssStyle(style, kEmSize, CssTextAlign::Right, kViewportWidth);
  CHECK(resolved.alignment == CssTextAlign::Right);
}

TEST_CASE("BlockStyle: \"Book's Style\" defers to embedded text-align") {
  const auto style =
      BlockStyle::fromCssStyle(styleWithAlign(CssTextAlign::Center), kEmSize, CssTextAlign::None, kViewportWidth);
  CHECK(style.alignment == CssTextAlign::Center);
  CHECK(style.textAlignDefined);
}

TEST_CASE("BlockStyle: \"Book's Style\" falls back to justify when the CSS defines none") {
  CssStyle style;
  const auto resolved = BlockStyle::fromCssStyle(style, kEmSize, CssTextAlign::None, kViewportWidth);
  CHECK(resolved.alignment == CssTextAlign::Justify);
  // Nothing defined the alignment, so an ancestor block may still supply one.
  CHECK_FALSE(resolved.textAlignDefined);
}

TEST_CASE("BlockStyle: textAlignDefined tracks the CSS, not the user setting") {
  // The caller marks its own blocks defined when the user has a preference;
  // the helper must report only what the stylesheet actually said, or block
  // inheritance would see a definition that no stylesheet made.
  CssStyle undefinedAlign;
  CHECK_FALSE(BlockStyle::fromCssStyle(undefinedAlign, kEmSize, CssTextAlign::Left, kViewportWidth).textAlignDefined);
  CHECK(BlockStyle::fromCssStyle(styleWithAlign(CssTextAlign::Right), kEmSize, CssTextAlign::Left, kViewportWidth)
            .textAlignDefined);
}
