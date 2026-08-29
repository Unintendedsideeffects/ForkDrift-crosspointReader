#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "ReaderInputPolicy.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromSideBtn;
};

inline PageTurnResult detectPageTurn(MappedInputManager& input) {
  // Front buttons fire on press when long-press behavior is OFF (faster response).
  const bool frontUsePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  // Side buttons fire on press only when long-press action is OFF (nothing to detect).
  const bool sideUsePress = SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_PRESS::SIDE_LONG_OFF;

  const bool sidePrev = sideUsePress ? input.wasPressed(MappedInputManager::Button::PageBack)
                                     : input.wasReleased(MappedInputManager::Button::PageBack);
  const bool sideNext = sideUsePress ? input.wasPressed(MappedInputManager::Button::PageForward)
                                     : input.wasReleased(MappedInputManager::Button::PageForward);
  const bool frontPrev = frontUsePress ? input.wasPressed(MappedInputManager::Button::Left)
                                       : input.wasReleased(MappedInputManager::Button::Left);
  const bool powerReleased = input.wasReleased(MappedInputManager::Button::Power);
  const bool shortPowerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN && powerReleased &&
                              input.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration();
  const bool longPowerTurn = SETTINGS.longPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN && powerReleased &&
                             input.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration();
  const bool powerTurn = shortPowerTurn || longPowerTurn;
  const bool frontNext = frontUsePress ? (input.wasPressed(MappedInputManager::Button::Right) || powerTurn)
                                       : (input.wasReleased(MappedInputManager::Button::Right) || powerTurn);

  const bool fromSide = (sidePrev || sideNext) && !(frontPrev || frontNext);
  return {sidePrev || frontPrev, sideNext || frontNext, fromSide};
}

enum class DualSideConfirmClassification { IGNORE, DISPATCH_QUICK_ACTION, FALLTHROUGH_TO_PAGE_TURN };

inline DualSideConfirmClassification classifyDualSideConfirmAction(
    const PhysicalConfirmRelease& release, CrossPointSettings::LONG_PRESS_MENU_ACTION configuredAction) {
  if (!release.active) {
    return DualSideConfirmClassification::IGNORE;
  }
  constexpr unsigned long longPressMenuMs = 600;
  if (configuredAction != CrossPointSettings::LONG_PRESS_MENU_ACTION::LONG_MENU_OFF &&
      release.durationMs >= longPressMenuMs) {
    return DualSideConfirmClassification::DISPATCH_QUICK_ACTION;
  }
  return DualSideConfirmClassification::FALLTHROUGH_TO_PAGE_TURN;
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
}

// As displayWithRefreshCycle(), but leaves the panel refreshing and returns. The caller
// MUST call renderer.finishDisplayBuffer() before touching the framebuffer or the panel
// again. Only valid for plain-text pages: the image and grayscale passes both keep
// drawing after the display call, which the async window forbids.
inline void displayWithRefreshCycleAsync(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBufferAsync(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBufferAsync();
    pagesUntilFullRefresh--;
  }
}

template <typename GrayFn, typename RestoreFn>
void renderAntiAliased(GfxRenderer& renderer, GrayFn&& grayFn, RestoreFn&& restoreFn) {
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  grayFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  grayFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  restoreFn();
  renderer.cleanupGrayscaleWithFrameBuffer();
}

}  // namespace ReaderUtils
