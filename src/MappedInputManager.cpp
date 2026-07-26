#include "MappedInputManager.h"

#include <FeatureFlags.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"

namespace {

#ifdef SIMULATOR
size_t buttonIndex(MappedInputManager::Button button) { return static_cast<size_t>(button); }
#endif

// Double-tap window for power button (Back action).
// Lowering this reduces latency for single-tap Confirm action.
// Intentional latency: a single tap fires Confirm only after this window expires
// without a second tap. Wall-clock millis() is used so frame rate has no effect.
constexpr unsigned long POWER_DOUBLE_TAP_MS = 250;

using ButtonIndex = uint8_t;

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

// Order matches CrossPointSettings::SIDE_BUTTON_LAYOUT.
constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};

bool isLandscapeOrientation(const uint8_t orientation) {
  return orientation == CrossPointSettings::LANDSCAPE_CW || orientation == CrossPointSettings::LANDSCAPE_CCW;
}

// The orientation that governs input remapping in the current context:
// in a reader, the reader's own orientation; otherwise the global UI orientation
// (PORTRAIT unless ENABLE_GLOBAL_LANDSCAPE is on and the user picked landscape).
uint8_t effectiveInputOrientation(const bool readerMode) {
  if (readerMode) {
    return SETTINGS.orientation;
  }
#if ENABLE_GLOBAL_LANDSCAPE
  return SETTINGS.uiOrientation;
#else
  return CrossPointSettings::PORTRAIT;
#endif
}

ButtonIndex invertFrontButtonPosition(const ButtonIndex button) {
  switch (button) {
    case HalGPIO::BTN_BACK:
      return HalGPIO::BTN_RIGHT;
    case HalGPIO::BTN_CONFIRM:
      return HalGPIO::BTN_LEFT;
    case HalGPIO::BTN_LEFT:
      return HalGPIO::BTN_CONFIRM;
    case HalGPIO::BTN_RIGHT:
      return HalGPIO::BTN_BACK;
    default:
      return button;
  }
}

ButtonIndex mapFrontButtonForOrientation(const ButtonIndex button, const ButtonIndex leftButton,
                                         const ButtonIndex rightButton, const uint8_t orientation) {
  const auto orientationMode =
      static_cast<CrossPointSettings::FRONT_BUTTON_ORIENTATION_AWARE>(SETTINGS.frontButtonOrientationAware);

  if (orientationMode == CrossPointSettings::FRONT_ORIENTATION_AWARE_ALL_BUTTONS &&
      orientation == CrossPointSettings::INVERTED) {
    return invertFrontButtonPosition(button);
  }

  if (orientationMode != CrossPointSettings::FRONT_ORIENTATION_AWARE_OFF && isLandscapeOrientation(orientation)) {
    if (button == leftButton) return rightButton;
    if (button == rightButton) return leftButton;
  }

  return button;
}

SideLayoutMap mapSideLayoutForOrientation(SideLayoutMap side, const uint8_t orientation) {
  if (SETTINGS.sideButtonOrientationAware && isLandscapeOrientation(orientation)) {
    std::swap(side.pageBack, side.pageForward);
  }
  return side;
}

bool isDualSideLayout() {
  return static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout) ==
         CrossPointSettings::LEFT_LEFT_RIGHT_RIGHT;
}

bool isPowerTapSelectEnabled() {
  return static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn) == CrossPointSettings::SELECT;
}

bool equalsLabel(const char* value, const char* expected) {
  return value != nullptr && expected != nullptr && strcmp(value, expected) == 0;
}
}  // namespace

void MappedInputManager::update() {
  gpio.update();

  bool isNowPressed = gpio.isPressed(HalGPIO::BTN_CONFIRM);

  physConfirmTracker.update(isNowPressed, millis());

#ifdef SIMULATOR
  if (simulatorPhysConfirmReleasePending) {
    physConfirmTracker.simulatorInjectRelease(simulatorPhysConfirmReleaseDuration);
    simulatorPhysConfirmReleasePending = false;
  }
#endif
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  // SIDE_BUTTONS_DISABLED: page-turn side buttons produce no action (upstream
  // #2105, for readers who hold the device by the buttons). Virtual injection
  // below still uses the PREV_NEXT mapping so remote page turns keep working.
  const bool sideDisabled = sideLayout == CrossPointSettings::SIDE_BUTTONS_DISABLED;
  if (sideDisabled) {
    sideLayout = CrossPointSettings::PREV_NEXT;  // keep table lookup in range
  }
  const uint8_t orientation = effectiveInputOrientation(readerMode);
  const auto side = mapSideLayoutForOrientation(kSideLayouts[sideLayout], orientation);

  const ButtonIndex btnLeft = SETTINGS.frontButtonLeft;
  const ButtonIndex btnRight = SETTINGS.frontButtonRight;
  const ButtonIndex mappedBack = mapFrontButtonForOrientation(SETTINGS.frontButtonBack, btnLeft, btnRight, orientation);
  const ButtonIndex mappedConfirm =
      mapFrontButtonForOrientation(SETTINGS.frontButtonConfirm, btnLeft, btnRight, orientation);
  const ButtonIndex mappedLeft = mapFrontButtonForOrientation(btnLeft, btnLeft, btnRight, orientation);
  const ButtonIndex mappedRight = mapFrontButtonForOrientation(btnRight, btnLeft, btnRight, orientation);

  switch (button) {
    case Button::Back:
      return (gpio.*fn)(mappedBack);
    case Button::Confirm:
      return (gpio.*fn)(mappedConfirm);
    case Button::Left:
      return (gpio.*fn)(mappedLeft);
    case Button::Right:
      return (gpio.*fn)(mappedRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      return !sideDisabled && (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      return !sideDisabled && (gpio.*fn)(side.pageForward);
  }

  return false;
}

void MappedInputManager::updatePowerTapState() {
  if (!isDualSideLayout() && !isPowerTapSelectEnabled()) {
    pendingPowerRelease = false;
    doubleTapReady = false;
    return;
  }

  const unsigned long now = millis();
  if (doubleTapReady && now - doubleTapReadyMs > POWER_DOUBLE_TAP_MS) {
    doubleTapReady = false;
  }

  if (!gpio.wasReleased(HalGPIO::BTN_POWER)) {
    powerReleaseConsumed = false;
    return;
  }

  if (powerReleaseConsumed) {
    return;
  }
  powerReleaseConsumed = true;

  if (gpio.getHeldTime() >= SETTINGS.getPowerButtonWakeDuration()) {
    // Long press detected - clear any pending short-tap state
    pendingPowerRelease = false;
    doubleTapReady = false;
    return;
  }

  if (pendingPowerRelease && now - pendingPowerReleaseMs <= POWER_DOUBLE_TAP_MS) {
    pendingPowerRelease = false;
    doubleTapReady = true;
    doubleTapReadyMs = now;
    return;
  }

  pendingPowerRelease = true;
  pendingPowerReleaseMs = now;
}

bool MappedInputManager::consumePowerConfirm() {
  updatePowerTapState();
  if (!pendingPowerRelease || doubleTapReady) {
    return false;
  }
  const unsigned long now = millis();
  if (now - pendingPowerReleaseMs > POWER_DOUBLE_TAP_MS) {
    pendingPowerRelease = false;
    return true;
  }
  return false;
}

bool MappedInputManager::consumePowerDoubleTap() {
  updatePowerTapState();
  if (!doubleTapReady) return false;
#if ENABLE_DOUBLE_TAP_ACTION
  const auto action = static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.doubleTapPwrBtn);
  if (action == CrossPointSettings::DOUBLE_TAP_BACK || action == CrossPointSettings::IGNORE) {
    return false;  // consumePowerBack() handles DOUBLE_TAP_BACK; IGNORE lets the tap expire silently
  }
#else
  return false;
#endif
  doubleTapReady = false;
  return true;
}

bool MappedInputManager::consumePowerBack() {
  updatePowerTapState();
  if (!doubleTapReady) {
    return false;
  }
#if ENABLE_DOUBLE_TAP_ACTION
  const auto action = static_cast<CrossPointSettings::SHORT_PWRBTN>(SETTINGS.doubleTapPwrBtn);
  if (action != CrossPointSettings::DOUBLE_TAP_BACK && action != CrossPointSettings::IGNORE) {
    return false;  // consumePowerDoubleTap() (called from main loop) owns this event
  }
#endif
  doubleTapReady = false;
  return true;
}

bool MappedInputManager::wasPressed(const Button button) {
#ifdef SIMULATOR
  if (simulatorPressed[buttonIndex(button)]) return true;
#endif
  if (button == Button::Confirm && consumePowerConfirm()) {
    return true;
  }
  if (button == Button::Back && consumePowerBack()) {
    return true;
  }
  if (readerMode && isDualSideLayout()) {
    if (button == Button::Left) {
      return gpio.wasPressed(HalGPIO::BTN_BACK) || gpio.wasPressed(HalGPIO::BTN_LEFT);
    }
    if (button == Button::Right) {
      return gpio.wasPressed(HalGPIO::BTN_CONFIRM) || gpio.wasPressed(HalGPIO::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) {
#ifdef SIMULATOR
  if (simulatorReleased[buttonIndex(button)]) return true;
#endif
  if (button == Button::Back && suppressBackRelease) {
    if (mapButton(button, &HalGPIO::wasReleased)) {
      suppressBackRelease = false;
      return false;
    }
  }
  if (button == Button::Confirm && consumePowerConfirm()) {
    return true;
  }
  if (button == Button::Back && consumePowerBack()) {
    return true;
  }
  if (readerMode && isDualSideLayout()) {
    if (button == Button::Left) {
      return gpio.wasReleased(HalGPIO::BTN_BACK) || gpio.wasReleased(HalGPIO::BTN_LEFT);
    }
    if (button == Button::Right) {
      return ReaderInputPolicy::resolveDualSideRightRelease(gpio.wasReleased(HalGPIO::BTN_CONFIRM),
                                                            gpio.wasReleased(HalGPIO::BTN_RIGHT), physConfirmTracker);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return mapButton(button, &HalGPIO::wasReleased);
}

void MappedInputManager::clearTransientState() {
  // Discard any queued virtual activations before clearing hardware edge events.
  for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
    (void)gpio.wasPressed(button);
    (void)gpio.wasReleased(button);
  }

  // Resample immediately so pressed/released edges from the previous activity
  // do not leak into the next screen after a transition.
  gpio.update();
  pendingPowerRelease = false;
  doubleTapReady = false;
  powerReleaseConsumed = false;
  suppressBackRelease = false;
  physConfirmTracker.clear();
#ifdef SIMULATOR
  simulatorPressed.fill(false);
  simulatorReleased.fill(false);
  simulatorHeld.fill(false);
  simulatorPhysConfirmReleasePending = false;
#endif
}

void MappedInputManager::injectVirtualActivation(const Button button) {
#ifndef SIMULATOR
  auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  if (sideLayout >= CrossPointSettings::SIDE_BUTTONS_DISABLED) {
    sideLayout = CrossPointSettings::PREV_NEXT;  // remote turns keep default mapping
  }
  const auto& side = kSideLayouts[sideLayout];
  switch (button) {
    case Button::PageForward:
      gpio.injectVirtualButton(side.pageForward);
      break;
    case Button::PageBack:
      gpio.injectVirtualButton(side.pageBack);
      break;
    case Button::Confirm:
      gpio.injectVirtualButton(SETTINGS.frontButtonConfirm);
      break;
    case Button::Back:
      gpio.injectVirtualButton(SETTINGS.frontButtonBack);
      break;
    case Button::Left:
      gpio.injectVirtualButton(SETTINGS.frontButtonLeft);
      break;
    case Button::Right:
      gpio.injectVirtualButton(SETTINGS.frontButtonRight);
      break;
    case Button::Up:
      gpio.injectVirtualButton(HalGPIO::BTN_UP);
      break;
    case Button::Down:
      gpio.injectVirtualButton(HalGPIO::BTN_DOWN);
      break;
    default:
      break;
  }
#else
  (void)button;
#endif
}

bool MappedInputManager::isPressed(const Button button) const {
  if (readerMode && isDualSideLayout()) {
    if (button == Button::Left) {
      return gpio.isPressed(HalGPIO::BTN_BACK) || gpio.isPressed(HalGPIO::BTN_LEFT);
    }
    if (button == Button::Right) {
      return gpio.isPressed(HalGPIO::BTN_CONFIRM) || gpio.isPressed(HalGPIO::BTN_RIGHT);
    }
    if (button == Button::Back || button == Button::Confirm) {
      return false;
    }
  }
  return mapButton(button, &HalGPIO::isPressed);
}

MappedInputManager::PhysicalConfirmRelease MappedInputManager::peekReaderDualSideConfirmRelease() const {
  if (readerMode && isDualSideLayout()) {
    return physConfirmTracker.peekRelease();
  }
  return {false, 0};
}

void MappedInputManager::consumeReaderDualSideConfirmRelease() {
  if (readerMode && isDualSideLayout()) {
    physConfirmTracker.consumeRelease();
  }
}

bool MappedInputManager::wasAnyPressed() const {
#ifdef SIMULATOR
  if (std::any_of(simulatorPressed.begin(), simulatorPressed.end(), [](bool b) { return b; })) return true;
#endif
  return gpio.wasAnyPressed();
}

bool MappedInputManager::wasAnyReleased() const {
#ifdef SIMULATOR
  if (std::any_of(simulatorReleased.begin(), simulatorReleased.end(), [](bool b) { return b; })) return true;
#endif
  return gpio.wasAnyReleased();
}

unsigned long MappedInputManager::getHeldTime() const {
#ifdef SIMULATOR
  unsigned long heldTime = gpio.getHeldTime();
  const unsigned long now = millis();
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
    if (simulatorHeld[i] && simulatorPressStart[i] > 0) {
      heldTime = std::max(heldTime, now - simulatorPressStart[i]);
    }
  }
  return heldTime;
#else
  return gpio.getHeldTime();
#endif
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  if (readerMode && isDualSideLayout()) {
    // In dual-side mode, front buttons map to logical Left/Right.
    // Reword generic vertical hints to match physical behavior.
    const bool verticalHints = equalsLabel(previous, "Up") && equalsLabel(next, "Down");
    const char* dualPrev = verticalHints ? "Left" : previous;
    const char* dualNext = verticalHints ? "Right" : next;
    return {dualPrev, dualPrev, dualNext, dualNext};
  }

  // Build the label order based on the configured hardware mapping (with orientation-aware remapping).
  const uint8_t orientation = effectiveInputOrientation(readerMode);
  const ButtonIndex btnLeft = SETTINGS.frontButtonLeft;
  const ButtonIndex btnRight = SETTINGS.frontButtonRight;
  const ButtonIndex mappedBack = mapFrontButtonForOrientation(SETTINGS.frontButtonBack, btnLeft, btnRight, orientation);
  const ButtonIndex mappedConfirm =
      mapFrontButtonForOrientation(SETTINGS.frontButtonConfirm, btnLeft, btnRight, orientation);
  const ButtonIndex mappedLeft = mapFrontButtonForOrientation(btnLeft, btnLeft, btnRight, orientation);
  const ButtonIndex mappedRight = mapFrontButtonForOrientation(btnRight, btnLeft, btnRight, orientation);

  auto labelForHardware = [&](ButtonIndex hw) -> const char* {
    if (hw == mappedBack) return back;
    if (hw == mappedConfirm) return confirm;
    if (hw == mappedLeft) return previous;
    if (hw == mappedRight) return next;
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}

#ifdef SIMULATOR
void MappedInputManager::simulatorInjectPress(const Button button) {
  const size_t idx = buttonIndex(button);
  simulatorPressed[idx] = true;
  simulatorReleased[idx] = false;
  simulatorHeld[idx] = true;
  simulatorPressStart[idx] = millis();
}

void MappedInputManager::simulatorInjectRelease(const Button button) {
  const size_t idx = buttonIndex(button);
  simulatorPressed[idx] = false;
  simulatorReleased[idx] = true;
  simulatorHeld[idx] = false;
}

void MappedInputManager::simulatorClearInputFrame() {
  simulatorPressed.fill(false);
  simulatorReleased.fill(false);
}

void MappedInputManager::simulatorInjectPhysicalConfirmRelease(unsigned long durationMs) {
  simulatorPhysConfirmReleasePending = true;
  simulatorPhysConfirmReleaseDuration = durationMs;
}
#endif
