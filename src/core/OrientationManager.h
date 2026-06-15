#pragma once

#include <CrossPointSettings.h>
#include <FeatureFlags.h>
#include <GfxRenderer.h>

// Global (non-reader) UI orientation control for the "global landscape" feature.
// Single source of truth: SETTINGS.uiOrientation. Independent of the reader's
// SETTINGS.orientation. Every function compile-folds to today's behavior when
// ENABLE_GLOBAL_LANDSCAPE is 0.
namespace OrientationManager {

// True only when global landscape is compiled in AND the user's UI orientation
// is a landscape mode. Compile-folds to `false` when the gate is OFF.
inline bool uiLandscapeActive() {
#if ENABLE_GLOBAL_LANDSCAPE
  return SETTINGS.uiOrientation == CrossPointSettings::LANDSCAPE_CW ||
         SETTINGS.uiOrientation == CrossPointSettings::LANDSCAPE_CCW;
#else
  return false;
#endif
}

// Apply the global UI orientation to the renderer.
// Gate ON: applies SETTINGS.uiOrientation. Gate OFF: forces Portrait — byte-identical
// to the `renderer.setOrientation(Portrait)` calls this replaces in plan 006.
inline void applyUiOrientation(GfxRenderer& renderer) {
#if ENABLE_GLOBAL_LANDSCAPE
  switch (SETTINGS.uiOrientation) {
    case CrossPointSettings::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    case CrossPointSettings::PORTRAIT:
    default:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
  }
#else
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
#endif
}

}  // namespace OrientationManager
