#pragma once
#include <FeatureFlags.h>

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

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  void onEnter() override;
  bool blocksBackgroundServer() override { return true; }
  bool showsGlobalStatusBar() const override { return false; }

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderSmartSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderImageSleepScreen(const std::string& imagePath) const;
  void renderTransparentSleepScreen() const;
#if ENABLE_READING_STATS
  void renderReadingStatsSleepScreen() const;
#endif
#if ENABLE_NOTES
  void renderNotesSleepScreen() const;
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
  bool tryRenderImagePath(const std::string& path) const;

  void drawLockIcon(int cx, int cy) const;
};
