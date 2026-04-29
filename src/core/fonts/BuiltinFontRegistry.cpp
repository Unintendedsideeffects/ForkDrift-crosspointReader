#include "core/fonts/BuiltinFontRegistry.h"

#include <EpdFont.h>
#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <builtinFonts/all.h>

#include "fontIds.h"

namespace core {
namespace {

EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);

#if ENABLE_BOOKERLY_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);

EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);

EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);
#endif

#if ENABLE_NOTOSANS_FONTS
EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);

EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);

EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);

EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);
#endif

#if ENABLE_OPENDYSLEXIC_FONTS
EpdFont opendyslexic8RegularFont(&opendyslexic_8_regular);
EpdFont opendyslexic8BoldFont(&opendyslexic_8_bold);
EpdFont opendyslexic8ItalicFont(&opendyslexic_8_italic);
EpdFont opendyslexic8BoldItalicFont(&opendyslexic_8_bolditalic);
EpdFontFamily opendyslexic8FontFamily(&opendyslexic8RegularFont, &opendyslexic8BoldFont, &opendyslexic8ItalicFont,
                                      &opendyslexic8BoldItalicFont);

EpdFont opendyslexic10RegularFont(&opendyslexic_10_regular);
EpdFont opendyslexic10BoldFont(&opendyslexic_10_bold);
EpdFont opendyslexic10ItalicFont(&opendyslexic_10_italic);
EpdFont opendyslexic10BoldItalicFont(&opendyslexic_10_bolditalic);
EpdFontFamily opendyslexic10FontFamily(&opendyslexic10RegularFont, &opendyslexic10BoldFont, &opendyslexic10ItalicFont,
                                       &opendyslexic10BoldItalicFont);

EpdFont opendyslexic12RegularFont(&opendyslexic_12_regular);
EpdFont opendyslexic12BoldFont(&opendyslexic_12_bold);
EpdFont opendyslexic12ItalicFont(&opendyslexic_12_italic);
EpdFont opendyslexic12BoldItalicFont(&opendyslexic_12_bolditalic);
EpdFontFamily opendyslexic12FontFamily(&opendyslexic12RegularFont, &opendyslexic12BoldFont, &opendyslexic12ItalicFont,
                                       &opendyslexic12BoldItalicFont);

EpdFont opendyslexic14RegularFont(&opendyslexic_14_regular);
EpdFont opendyslexic14BoldFont(&opendyslexic_14_bold);
EpdFont opendyslexic14ItalicFont(&opendyslexic_14_italic);
EpdFont opendyslexic14BoldItalicFont(&opendyslexic_14_bolditalic);
EpdFontFamily opendyslexic14FontFamily(&opendyslexic14RegularFont, &opendyslexic14BoldFont, &opendyslexic14ItalicFont,
                                       &opendyslexic14BoldItalicFont);
#endif

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

void registerUiFontFamilies(GfxRenderer& renderer) {
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
}

}  // namespace

void BuiltinFontRegistry::registerUiFonts(GfxRenderer& renderer) { registerUiFontFamilies(renderer); }

bool BuiltinFontRegistry::registerAllFonts(GfxRenderer& renderer) {
  registerUiFontFamilies(renderer);

  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);

#if ENABLE_BOOKERLY_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);
#endif

#if ENABLE_NOTOSANS_FONTS
  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif

#if ENABLE_OPENDYSLEXIC_FONTS
  renderer.insertFont(OPENDYSLEXIC_8_FONT_ID, opendyslexic8FontFamily);
  renderer.insertFont(OPENDYSLEXIC_10_FONT_ID, opendyslexic10FontFamily);
  renderer.insertFont(OPENDYSLEXIC_12_FONT_ID, opendyslexic12FontFamily);
  renderer.insertFont(OPENDYSLEXIC_14_FONT_ID, opendyslexic14FontFamily);
#endif

  return true;
}

}  // namespace core
