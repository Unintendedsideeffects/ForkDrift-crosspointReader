#pragma once

// Firmware-compiled ("built-in") font wiring, extracted from main.cpp. These
// are the families baked into the binary, as opposed to the .cpfont families
// loaded from SD at runtime by SdCardFontSystem.
//
// Each family is an independent compile feature gated by an ENABLE_*_FONTS flag
// (see FeatureFlags.h), so the set is not fixed: a default build compiles only
// Noto Serif (ENABLE_BOOKERLY_FONTS, the lone flag defaulting on) plus the
// always-present Ubuntu UI/small fonts; Noto Sans, OpenDyslexic, Lexend Deca,
// Bitter, and ChareInk are opt-in. OMIT_FONTS further drops the larger reading
// families to reclaim flash.
//
// Definitions live in BuiltinFonts.cpp as static-storage EpdFont/EpdFontFamily
// globals because GfxRenderer keeps EpdFontFamily values holding raw pointers
// back into the EpdFont objects, which must therefore outlive registration.
// main.cpp only needs the single entry point below, called once during font
// setup to register whichever families this build actually compiled.

class GfxRenderer;

void registerBuiltinFonts(GfxRenderer& renderer);
