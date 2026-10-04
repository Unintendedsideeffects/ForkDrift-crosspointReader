#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalDeviceInfo.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class AboutActivity final : public Activity {
 public:
  AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowGlobalHomeSwipeGesture() const override { return false; }
#ifdef SIMULATOR
  int simulatorVisibleRows() const { return visibleRows; }
  int simulatorRowCount() const;
  int simulatorTopIndex() const { return topIndex; }
  const HalDeviceInfo::Snapshot& simulatorSnapshot() const { return snapshot; }
#endif

 private:
  using UiApp = freeink::ui::FreeInkApp<1, 1>;
  ButtonNavigator buttonNavigator;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  HalDeviceInfo::Snapshot snapshot;
  // Reused by the render-only row provider; no strings/arrays per row or frame.
  char valueBuffer[96]{};
  int topIndex = 0;
  int visibleRows = 1;
  static void aboutScreen(UiApp::ScreenType& screen, void* user);
  static void provideRow(void* user, uint16_t index, freeink::ui::ListItem& item);
};
