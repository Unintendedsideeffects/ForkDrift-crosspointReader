#include "Epub/css/CssStyle.h"
#include "Epub/parsers/InlineStyleEntry.h"
#include "doctest/doctest.h"

TEST_CASE("applyVerticalAlignToEntry copies super and sub") {
  CssStyle superStyle;
  superStyle.verticalAlign = CssVerticalAlign::Super;
  superStyle.defined.verticalAlign = 1;
  StyleStackEntry superEntry;
  applyVerticalAlignToEntry(superEntry, superStyle);
  CHECK(superEntry.hasSup);
  CHECK(superEntry.sup);
  CHECK_FALSE(superEntry.hasSub);

  CssStyle subStyle;
  subStyle.verticalAlign = CssVerticalAlign::Sub;
  subStyle.defined.verticalAlign = 1;
  StyleStackEntry subEntry;
  applyVerticalAlignToEntry(subEntry, subStyle);
  CHECK(subEntry.hasSub);
  CHECK(subEntry.sub);
  CHECK_FALSE(subEntry.hasSup);
}

TEST_CASE("applyVerticalAlignToEntry ignores baseline and unset") {
  StyleStackEntry unsetEntry;
  applyVerticalAlignToEntry(unsetEntry, CssStyle{});
  CHECK_FALSE(unsetEntry.hasSup);
  CHECK_FALSE(unsetEntry.hasSub);

  CssStyle baseline;
  baseline.verticalAlign = CssVerticalAlign::Baseline;
  baseline.defined.verticalAlign = 1;
  StyleStackEntry baselineEntry;
  applyVerticalAlignToEntry(baselineEntry, baseline);
  CHECK_FALSE(baselineEntry.hasSup);
  CHECK_FALSE(baselineEntry.hasSub);
}

TEST_CASE("fillInternalLinkStyleEntry applies underline, direction, and super") {
  CssStyle css;
  css.verticalAlign = CssVerticalAlign::Super;
  css.defined.verticalAlign = 1;
  css.direction = CssTextDirection::Rtl;
  css.defined.direction = 1;
  StyleStackEntry entry;
  fillInternalLinkStyleEntry(entry, css);
  CHECK(entry.hasUnderline);
  CHECK(entry.underline);
  CHECK(entry.hasDirection);
  CHECK(entry.direction == CssTextDirection::Rtl);
  CHECK(entry.hasSup);
  CHECK(entry.sup);
  CHECK_FALSE(entry.hasSub);
}
