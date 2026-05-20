#pragma once

#include <FeatureFlags.h>

#if ENABLE_BOOKERLY_FONTS
#include <builtinFonts/notoserif_12_bold.h>
#include <builtinFonts/notoserif_12_bolditalic.h>
#include <builtinFonts/notoserif_12_italic.h>
#include <builtinFonts/notoserif_12_regular.h>
#include <builtinFonts/notoserif_14_bold.h>
#include <builtinFonts/notoserif_14_bolditalic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_16_bold.h>
#include <builtinFonts/notoserif_16_bolditalic.h>
#include <builtinFonts/notoserif_16_italic.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/notoserif_18_bold.h>
#include <builtinFonts/notoserif_18_bolditalic.h>
#include <builtinFonts/notoserif_18_italic.h>
#include <builtinFonts/notoserif_18_regular.h>
#endif  // ENABLE_BOOKERLY_FONTS

// notosans_8_regular is always included — used as SMALL_FONT_ID for path/label text.
#include <builtinFonts/notosans_8_regular.h>

#if ENABLE_NOTOSANS_FONTS
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_bolditalic.h>
#include <builtinFonts/notosans_12_italic.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_bolditalic.h>
#include <builtinFonts/notosans_14_italic.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notosans_16_bold.h>
#include <builtinFonts/notosans_16_bolditalic.h>
#include <builtinFonts/notosans_16_italic.h>
#include <builtinFonts/notosans_16_regular.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_bolditalic.h>
#include <builtinFonts/notosans_18_italic.h>
#include <builtinFonts/notosans_18_regular.h>
#endif  // ENABLE_NOTOSANS_FONTS

#if ENABLE_OPENDYSLEXIC_FONTS
#include <builtinFonts/opendyslexic_8_bold.h>
#include <builtinFonts/opendyslexic_8_bolditalic.h>
#include <builtinFonts/opendyslexic_8_italic.h>
#include <builtinFonts/opendyslexic_8_regular.h>
#include <builtinFonts/opendyslexic_10_bold.h>
#include <builtinFonts/opendyslexic_10_bolditalic.h>
#include <builtinFonts/opendyslexic_10_italic.h>
#include <builtinFonts/opendyslexic_10_regular.h>
#include <builtinFonts/opendyslexic_12_bold.h>
#include <builtinFonts/opendyslexic_12_bolditalic.h>
#include <builtinFonts/opendyslexic_12_italic.h>
#include <builtinFonts/opendyslexic_12_regular.h>
#include <builtinFonts/opendyslexic_14_bold.h>
#include <builtinFonts/opendyslexic_14_bolditalic.h>
#include <builtinFonts/opendyslexic_14_italic.h>
#include <builtinFonts/opendyslexic_14_regular.h>
#endif  // ENABLE_OPENDYSLEXIC_FONTS

// Ubuntu is the UI font — always compiled in regardless of reading font selection.
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>

#if ENABLE_LEXENDDECA_FONTS
#include <builtinFonts/lexenddeca_12_bold.h>
#include <builtinFonts/lexenddeca_12_bolditalic.h>
#include <builtinFonts/lexenddeca_12_italic.h>
#include <builtinFonts/lexenddeca_12_regular.h>
#include <builtinFonts/lexenddeca_14_bold.h>
#include <builtinFonts/lexenddeca_14_bolditalic.h>
#include <builtinFonts/lexenddeca_14_italic.h>
#include <builtinFonts/lexenddeca_14_regular.h>
#include <builtinFonts/lexenddeca_16_bold.h>
#include <builtinFonts/lexenddeca_16_bolditalic.h>
#include <builtinFonts/lexenddeca_16_italic.h>
#include <builtinFonts/lexenddeca_16_regular.h>
#include <builtinFonts/lexenddeca_18_bold.h>
#include <builtinFonts/lexenddeca_18_bolditalic.h>
#include <builtinFonts/lexenddeca_18_italic.h>
#include <builtinFonts/lexenddeca_18_regular.h>
#endif  // ENABLE_LEXENDDECA_FONTS

#if ENABLE_BITTER_FONTS
#include <builtinFonts/bitter_12_bold.h>
#include <builtinFonts/bitter_12_bolditalic.h>
#include <builtinFonts/bitter_12_italic.h>
#include <builtinFonts/bitter_12_regular.h>
#include <builtinFonts/bitter_14_bold.h>
#include <builtinFonts/bitter_14_bolditalic.h>
#include <builtinFonts/bitter_14_italic.h>
#include <builtinFonts/bitter_14_regular.h>
#include <builtinFonts/bitter_16_bold.h>
#include <builtinFonts/bitter_16_bolditalic.h>
#include <builtinFonts/bitter_16_italic.h>
#include <builtinFonts/bitter_16_regular.h>
#include <builtinFonts/bitter_18_bold.h>
#include <builtinFonts/bitter_18_bolditalic.h>
#include <builtinFonts/bitter_18_italic.h>
#include <builtinFonts/bitter_18_regular.h>
#endif  // ENABLE_BITTER_FONTS

#if ENABLE_CHAREINK_FONTS
#include <builtinFonts/charein_12_bold.h>
#include <builtinFonts/charein_12_bolditalic.h>
#include <builtinFonts/charein_12_italic.h>
#include <builtinFonts/charein_12_regular.h>
#include <builtinFonts/charein_14_bold.h>
#include <builtinFonts/charein_14_bolditalic.h>
#include <builtinFonts/charein_14_italic.h>
#include <builtinFonts/charein_14_regular.h>
#include <builtinFonts/charein_16_bold.h>
#include <builtinFonts/charein_16_bolditalic.h>
#include <builtinFonts/charein_16_italic.h>
#include <builtinFonts/charein_16_regular.h>
#include <builtinFonts/charein_18_bold.h>
#include <builtinFonts/charein_18_bolditalic.h>
#include <builtinFonts/charein_18_italic.h>
#include <builtinFonts/charein_18_regular.h>
#endif  // ENABLE_CHAREINK_FONTS
