#pragma once
#include <FeatureFlags.h>

#include <cstdint>
#include <string>

#include "activities/Activity.h"

class Bitmap;

struct SleepImageValidationStats {
  int valid = 0;
  int invalid = 0;
};

void invalidateSleepImageCache();
SleepImageValidationStats validateSleepImagesWithStats();
int validateAndCountSleepImages();

struct CoverDrawRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool valid = false;
};

enum class PinnedImageRenderStage : uint8_t {
  NotAttempted,
  BitmapOpen,
  BitmapHeaders,
  DecoderLookup,
  Dimensions,
  BwDecode,
  GrayscaleLsb,
  GrayscaleMsb,
  Complete,
};

const char* pinnedImageRenderStageName(PinnedImageRenderStage stage);

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;
  bool selectedPinnedImageRendered() const { return selectedPinnedImageRendered_; }
  PinnedImageRenderStage selectedPinnedImageRenderStage() const { return selectedPinnedImageRenderStage_; }
  uint32_t selectedPinnedImageRenderFreeHeap() const { return selectedPinnedImageRenderFreeHeap_; }
  uint32_t selectedPinnedImageRenderMaxAllocHeap() const { return selectedPinnedImageRenderMaxAllocHeap_; }
  bool blocksBackgroundServer() override { return true; }
  bool showsGlobalStatusBar() const override { return false; }

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  uint8_t effectiveSleepMode() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, CoverDrawRect* drawnRect = nullptr) const;
  bool renderImageSleepScreen(const std::string& imagePath, CoverDrawRect* drawnRect = nullptr) const;
  void renderTransparentSleepScreen() const;
#if ENABLE_READING_STATS
  void renderReadingStatsSleepScreen() const;
#endif
#if ENABLE_NOTES
  void renderNotesSleepScreen() const;
#endif
#if ENABLE_ANKI_SUPPORT
  void renderAnkiSleepScreen() const;
#endif
#if ENABLE_TODO_PLANNER
  void renderPlannerSleepScreen() const;
#endif
#if ENABLE_ROMAN_CLOCK_SLEEP
  void renderRomanClockSleepScreen() const;
#endif
#if ENABLE_HAIKU_CLOCK
  void renderHaikuClockSleepScreen() const;
#endif
#if ENABLE_POKEMON_PARTY
  // Renders the open book's cover full-screen with its assigned Pokémon (current
  // evolution stage for the reading-progress level) overlaid in the corner.
  // Returns false when there is no current book / assignment to composite.
  bool renderPokemonCoverSleepScreen() const;
  bool drawPokemonCoverOverlay(const std::string& bookPath) const;
#endif
  bool tryRenderCurrentBookCover() const;
  bool tryRenderImagePath(const std::string& path, CoverDrawRect* drawnRect = nullptr) const;
  void recordPinnedImageRenderStage(PinnedImageRenderStage stage) const;

  void drawLockIcon(int cx, int cy) const;

  // True only when the configured pinned file reached the display renderer.
  // A default/random fallback deliberately leaves this false.
  mutable bool selectedPinnedImageRendered_ = false;
  mutable PinnedImageRenderStage selectedPinnedImageRenderStage_ = PinnedImageRenderStage::NotAttempted;
  mutable uint32_t selectedPinnedImageRenderFreeHeap_ = 0;
  mutable uint32_t selectedPinnedImageRenderMaxAllocHeap_ = 0;
};
