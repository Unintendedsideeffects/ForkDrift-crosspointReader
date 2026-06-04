#include "BuiltinFonts.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>

#include "FeatureFlags.h"
#include "fontIds.h"

#if ENABLE_BOOKERLY_FONTS
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#endif  // ENABLE_BOOKERLY_FONTS
#ifndef OMIT_FONTS
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
#endif  // ENABLE_BOOKERLY_FONTS

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
#endif  // ENABLE_NOTOSANS_FONTS

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
#endif  // ENABLE_OPENDYSLEXIC_FONTS
#endif  // OMIT_FONTS

#if ENABLE_LEXENDDECA_FONTS
EpdFont lexenddeca12RegularFont(&lexenddeca_12_regular);
EpdFont lexenddeca12BoldFont(&lexenddeca_12_bold);
EpdFont lexenddeca12ItalicFont(&lexenddeca_12_italic);
EpdFont lexenddeca12BoldItalicFont(&lexenddeca_12_bolditalic);
EpdFontFamily lexenddeca12FontFamily(&lexenddeca12RegularFont, &lexenddeca12BoldFont, &lexenddeca12ItalicFont,
                                     &lexenddeca12BoldItalicFont);
EpdFont lexenddeca14RegularFont(&lexenddeca_14_regular);
EpdFont lexenddeca14BoldFont(&lexenddeca_14_bold);
EpdFont lexenddeca14ItalicFont(&lexenddeca_14_italic);
EpdFont lexenddeca14BoldItalicFont(&lexenddeca_14_bolditalic);
EpdFontFamily lexenddeca14FontFamily(&lexenddeca14RegularFont, &lexenddeca14BoldFont, &lexenddeca14ItalicFont,
                                     &lexenddeca14BoldItalicFont);
EpdFont lexenddeca16RegularFont(&lexenddeca_16_regular);
EpdFont lexenddeca16BoldFont(&lexenddeca_16_bold);
EpdFont lexenddeca16ItalicFont(&lexenddeca_16_italic);
EpdFont lexenddeca16BoldItalicFont(&lexenddeca_16_bolditalic);
EpdFontFamily lexenddeca16FontFamily(&lexenddeca16RegularFont, &lexenddeca16BoldFont, &lexenddeca16ItalicFont,
                                     &lexenddeca16BoldItalicFont);
EpdFont lexenddeca18RegularFont(&lexenddeca_18_regular);
EpdFont lexenddeca18BoldFont(&lexenddeca_18_bold);
EpdFont lexenddeca18ItalicFont(&lexenddeca_18_italic);
EpdFont lexenddeca18BoldItalicFont(&lexenddeca_18_bolditalic);
EpdFontFamily lexenddeca18FontFamily(&lexenddeca18RegularFont, &lexenddeca18BoldFont, &lexenddeca18ItalicFont,
                                     &lexenddeca18BoldItalicFont);
#endif  // ENABLE_LEXENDDECA_FONTS

#if ENABLE_BITTER_FONTS
EpdFont bitter12RegularFont(&bitter_12_regular);
EpdFont bitter12BoldFont(&bitter_12_bold);
EpdFont bitter12ItalicFont(&bitter_12_italic);
EpdFont bitter12BoldItalicFont(&bitter_12_bolditalic);
EpdFontFamily bitter12FontFamily(&bitter12RegularFont, &bitter12BoldFont, &bitter12ItalicFont, &bitter12BoldItalicFont);
EpdFont bitter14RegularFont(&bitter_14_regular);
EpdFont bitter14BoldFont(&bitter_14_bold);
EpdFont bitter14ItalicFont(&bitter_14_italic);
EpdFont bitter14BoldItalicFont(&bitter_14_bolditalic);
EpdFontFamily bitter14FontFamily(&bitter14RegularFont, &bitter14BoldFont, &bitter14ItalicFont, &bitter14BoldItalicFont);
EpdFont bitter16RegularFont(&bitter_16_regular);
EpdFont bitter16BoldFont(&bitter_16_bold);
EpdFont bitter16ItalicFont(&bitter_16_italic);
EpdFont bitter16BoldItalicFont(&bitter_16_bolditalic);
EpdFontFamily bitter16FontFamily(&bitter16RegularFont, &bitter16BoldFont, &bitter16ItalicFont, &bitter16BoldItalicFont);
EpdFont bitter18RegularFont(&bitter_18_regular);
EpdFont bitter18BoldFont(&bitter_18_bold);
EpdFont bitter18ItalicFont(&bitter_18_italic);
EpdFont bitter18BoldItalicFont(&bitter_18_bolditalic);
EpdFontFamily bitter18FontFamily(&bitter18RegularFont, &bitter18BoldFont, &bitter18ItalicFont, &bitter18BoldItalicFont);
#endif  // ENABLE_BITTER_FONTS

#if ENABLE_CHAREINK_FONTS
EpdFont charein12RegularFont(&charein_12_regular);
EpdFont charein12BoldFont(&charein_12_bold);
EpdFont charein12ItalicFont(&charein_12_italic);
EpdFont charein12BoldItalicFont(&charein_12_bolditalic);
EpdFontFamily charein12FontFamily(&charein12RegularFont, &charein12BoldFont, &charein12ItalicFont,
                                  &charein12BoldItalicFont);
EpdFont charein14RegularFont(&charein_14_regular);
EpdFont charein14BoldFont(&charein_14_bold);
EpdFont charein14ItalicFont(&charein_14_italic);
EpdFont charein14BoldItalicFont(&charein_14_bolditalic);
EpdFontFamily charein14FontFamily(&charein14RegularFont, &charein14BoldFont, &charein14ItalicFont,
                                  &charein14BoldItalicFont);
EpdFont charein16RegularFont(&charein_16_regular);
EpdFont charein16BoldFont(&charein_16_bold);
EpdFont charein16ItalicFont(&charein_16_italic);
EpdFont charein16BoldItalicFont(&charein_16_bolditalic);
EpdFontFamily charein16FontFamily(&charein16RegularFont, &charein16BoldFont, &charein16ItalicFont,
                                  &charein16BoldItalicFont);
EpdFont charein18RegularFont(&charein_18_regular);
EpdFont charein18BoldFont(&charein_18_bold);
EpdFont charein18ItalicFont(&charein_18_italic);
EpdFont charein18BoldItalicFont(&charein_18_bolditalic);
EpdFontFamily charein18FontFamily(&charein18RegularFont, &charein18BoldFont, &charein18ItalicFont,
                                  &charein18BoldItalicFont);
#endif  // ENABLE_CHAREINK_FONTS

EpdFont smallFont(&ubuntu_10_regular);
EpdFontFamily smallFontFamily(&smallFont);
EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);
EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

void registerBuiltinFonts(GfxRenderer& renderer) {
#if ENABLE_BOOKERLY_FONTS
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#endif  // ENABLE_BOOKERLY_FONTS
#ifndef OMIT_FONTS
#if ENABLE_BOOKERLY_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);
#endif  // ENABLE_BOOKERLY_FONTS
#if ENABLE_NOTOSANS_FONTS
  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // ENABLE_NOTOSANS_FONTS
#if ENABLE_OPENDYSLEXIC_FONTS
  renderer.insertFont(OPENDYSLEXIC_8_FONT_ID, opendyslexic8FontFamily);
  renderer.insertFont(OPENDYSLEXIC_10_FONT_ID, opendyslexic10FontFamily);
  renderer.insertFont(OPENDYSLEXIC_12_FONT_ID, opendyslexic12FontFamily);
  renderer.insertFont(OPENDYSLEXIC_14_FONT_ID, opendyslexic14FontFamily);
#endif  // ENABLE_OPENDYSLEXIC_FONTS
#endif  // OMIT_FONTS
#if ENABLE_LEXENDDECA_FONTS
  renderer.insertFont(LEXENDDECA_12_FONT_ID, lexenddeca12FontFamily);
  renderer.insertFont(LEXENDDECA_14_FONT_ID, lexenddeca14FontFamily);
  renderer.insertFont(LEXENDDECA_16_FONT_ID, lexenddeca16FontFamily);
  renderer.insertFont(LEXENDDECA_18_FONT_ID, lexenddeca18FontFamily);
#endif
#if ENABLE_BITTER_FONTS
  renderer.insertFont(BITTER_12_FONT_ID, bitter12FontFamily);
  renderer.insertFont(BITTER_14_FONT_ID, bitter14FontFamily);
  renderer.insertFont(BITTER_16_FONT_ID, bitter16FontFamily);
  renderer.insertFont(BITTER_18_FONT_ID, bitter18FontFamily);
#endif
#if ENABLE_CHAREINK_FONTS
  renderer.insertFont(CHAREINK_12_FONT_ID, charein12FontFamily);
  renderer.insertFont(CHAREINK_14_FONT_ID, charein14FontFamily);
  renderer.insertFont(CHAREINK_16_FONT_ID, charein16FontFamily);
  renderer.insertFont(CHAREINK_18_FONT_ID, charein18FontFamily);
#endif
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
}
