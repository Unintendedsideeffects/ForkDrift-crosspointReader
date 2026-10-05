#include <EpdFontFamily.h>
#include <Utf8.h>
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/ui_symbols_10.h>
#include <gtest/gtest.h>

namespace {
constexpr uint32_t POWER = 0x23FB;
const EpdFont symbols(&ui_symbols_10);
const EpdFont smallRegular(&inter_10_regular), smallBold(&inter_10_bold);
const EpdFont largeRegular(&inter_12_regular), largeBold(&inter_12_bold);
const EpdFontFamily small(&smallRegular, &smallBold, nullptr, nullptr, &symbols);
const EpdFontFamily large(&largeRegular, &largeBold, nullptr, nullptr, &symbols);
}  // namespace

TEST(UiSymbolFallback, ContainsExactlyOneGlyph) {
  EXPECT_EQ(sizeof(ui_symbols_10Glyphs) / sizeof(ui_symbols_10Glyphs[0]), 1u);
  EXPECT_EQ(sizeof(ui_symbols_10Intervals) / sizeof(ui_symbols_10Intervals[0]), 1u);
  EXPECT_EQ(ui_symbols_10Intervals[0].first, POWER);
  EXPECT_TRUE(symbols.hasCodepoint(POWER));
  EXPECT_FALSE(symbols.hasCodepoint('A'));
}

TEST(UiSymbolFallback, SharesTheSameRasterAtBothScalesAndStyles) {
  for (const auto* family : {&small, &large}) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      const auto glyph = family->getGlyphData(POWER, style);
      EXPECT_EQ(glyph.fontData, &ui_symbols_10);
      EXPECT_EQ(glyph.glyph, symbols.findGlyph(POWER));
      EXPECT_TRUE(family->hasCodepoint(POWER, style));
      EXPECT_EQ(family->getFallbackCodepoint(POWER, style), POWER);
      int width = 0, height = 0;
      family->getTextDimensions("⏻", &width, &height, style);
      EXPECT_EQ(width, 18);
      EXPECT_EQ(height, 18);
    }
  }
}

TEST(UiSymbolFallback, PreservesNormalGlyphsAndMissingGlyphBehavior) {
  EXPECT_EQ(small.getGlyphData('A').fontData, &inter_10_regular);
  EXPECT_EQ(large.getGlyphData('*', EpdFontFamily::BOLD).fontData, &inter_12_bold);
  const EpdFontFamily noFallback(&smallRegular);
  EXPECT_FALSE(noFallback.hasCodepoint(POWER));
  EXPECT_EQ(noFallback.getGlyphData(POWER).glyph, smallRegular.getGlyph(REPLACEMENT_GLYPH));
}

namespace {
constexpr EpdUnicodeInterval cjkIntervals[] = {{0x4E00, 0x4E01, 0}};
constexpr EpdGlyph cjkGlyphs[] = {{18, 18, 20 * 16, 0, 16, 81, 0}, {18, 18, 20 * 16, 0, 16, 81, 0}};
constexpr EpdGlyph cjkBoldGlyphs[] = {{19, 18, 21 * 16, 0, 16, 86, 0}};
constexpr EpdUnicodeInterval cjkBoldIntervals[] = {{0x4E00, 0x4E00, 0}};
EpdFontData cjkData(const EpdGlyph* glyphs, const EpdUnicodeInterval* intervals) {
  EpdFontData data{};
  data.glyph = glyphs;
  data.intervals = intervals;
  data.intervalCount = 1;
  data.advanceY = 24;
  data.ascender = 18;
  data.descender = -4;
  return data;
}
const auto cjkRegularData = cjkData(cjkGlyphs, cjkIntervals);
const auto cjkBoldData = cjkData(cjkBoldGlyphs, cjkBoldIntervals);
const EpdFont cjkRegular(&cjkRegularData), cjkBold(&cjkBoldData);
}  // namespace

TEST(FilenameFallback, PreservesLatinAndSharedSymbolsWithinMixedText) {
  const auto composite = small.withFallbackFonts(&cjkRegular, &cjkBold);
  EXPECT_EQ(composite.getGlyphData('A').fontData, &inter_10_regular);
  EXPECT_EQ(composite.getGlyphData('A', EpdFontFamily::BOLD).fontData, &inter_10_bold);
  EXPECT_EQ(composite.getGlyphData(POWER, EpdFontFamily::BOLD).fontData, &ui_symbols_10);
  EXPECT_EQ(composite.getGlyphData(0x4E00).fontData, &cjkRegularData);
  EXPECT_EQ(composite.getGlyphData(0x4E00, EpdFontFamily::BOLD).fontData, &cjkBoldData);
  EXPECT_EQ(composite.getData()->ascender, small.getData()->ascender);
  int primaryW = 0, primaryH = 0, compositeW = 0, compositeH = 0;
  small.getTextDimensions("Latin Volume 2", &primaryW, &primaryH);
  composite.getTextDimensions("Latin Volume 2", &compositeW, &compositeH);
  EXPECT_EQ(primaryW, compositeW);
  EXPECT_EQ(primaryH, compositeH);
}

TEST(FilenameFallback, UsesRegularWhenBoldIsMissingOrLacksAGlyph) {
  const auto withoutBold = small.withFallbackFonts(&cjkRegular);
  EXPECT_EQ(withoutBold.getGlyphData(0x4E00, EpdFontFamily::BOLD).fontData, &cjkRegularData);
  const auto partialBold = small.withFallbackFonts(&cjkRegular, &cjkBold);
  EXPECT_EQ(partialBold.getGlyphData(0x4E01, EpdFontFamily::BOLD).fontData, &cjkRegularData);
  EXPECT_TRUE(partialBold.hasCodepoint(0x4E01, EpdFontFamily::BOLD));
}

TEST(FilenameFallback, MeasuresFallbackGlyphsAndPreservesMissingGlyphBehavior) {
  const auto composite = small.withFallbackFonts(&cjkRegular);
  int width = 0, height = 0;
  composite.getTextDimensions("一丁", &width, &height);
  EXPECT_EQ(width, 38);  // 20-pixel advance plus the last glyph's 18-pixel ink
  EXPECT_EQ(height, 18);
  EXPECT_EQ(composite.getFallbackCodepoint(0x4E00), 0x4E00u);
  EXPECT_EQ(composite.getGlyphData(0x4E02).glyph, smallRegular.getGlyph(REPLACEMENT_GLYPH));
  EXPECT_EQ(composite.getKerning('A', 0x4E00), 0);
  EXPECT_EQ(composite.getKerning(0x4E00, 'V'), 0);
}
