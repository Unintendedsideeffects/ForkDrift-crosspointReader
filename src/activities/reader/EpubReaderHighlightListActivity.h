#pragma once

#include <FeatureFlags.h>

#if ENABLE_ANNOTATIONS

#include <vector>

#include "../Activity.h"
#include "util/AnnotationStore.h"
#include "util/ButtonNavigator.h"

class EpubReaderHighlightListActivity final : public Activity {
 public:
  explicit EpubReaderHighlightListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EpubReaderHighlightList", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::vector<Annotation> annotations;
  int selectedIndex = 0;
  bool longPressConfirmHandled = false;
  ButtonNavigator buttonNavigator;

  void deleteSelectedAnnotation();
  void showHighlightActionMenu(bool ignoreInitialConfirmRelease = false);
  int getPageItems() const;
};

#endif  // ENABLE_ANNOTATIONS
