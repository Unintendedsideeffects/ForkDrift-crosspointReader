#pragma once

#include <HalGPIO.h>

#include "activities/reader/ReaderInputPolicy.h"

#ifdef SIMULATOR
#include <array>
#endif

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward, NavNext, NavPrevious };

  using PhysicalConfirmRelease = ::PhysicalConfirmRelease;

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::PageForward) + 1;

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update();
  void setReaderMode(bool enabled) { readerMode = enabled; }
  void suppressNextBackRelease() { suppressBackRelease = true; }
  bool wasPressed(Button button);
  bool wasReleased(Button button);
  bool isPressed(Button button) const;
  void clearTransientState();
  // Inject a one-frame virtual activation for the given button.
  // wasPressed() and wasReleased() both report it, repeatably, until the main
  // loop calls HalGPIO::drainVirtualMask() at end of frame — matching how the
  // SDK latches physical button edges, so an activity that polls the same
  // button twice in a frame sees it both times.
  void injectVirtualActivation(Button button);
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;
  // Consume a pending double-tap for a configurable action.
  // Returns true and clears the double-tap state when ENABLE_DOUBLE_TAP_ACTION=1 and the
  // configured doubleTapPwrBtn action is not DOUBLE_TAP_BACK or IGNORE.
  // Call from the main loop BEFORE activityManager.loop() so activities never see it as Back.
  bool consumePowerDoubleTap();

  PhysicalConfirmRelease peekReaderDualSideConfirmRelease() const;
  void consumeReaderDualSideConfirmRelease();

#ifdef SIMULATOR
  void simulatorInjectPress(Button button);
  void simulatorInjectRelease(Button button);
  void simulatorClearInputFrame();
  void simulatorInjectPhysicalConfirmRelease(unsigned long durationMs);
#endif

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  bool readerMode = false;
  mutable bool suppressBackRelease = false;
  unsigned long pendingPowerReleaseMs = 0;
  unsigned long doubleTapReadyMs = 0;
  bool pendingPowerRelease = false;
  bool doubleTapReady = false;
  bool powerReleaseConsumed = false;

  PhysicalConfirmTracker physConfirmTracker;

#ifdef SIMULATOR
  std::array<bool, BUTTON_COUNT> simulatorPressed{};
  std::array<bool, BUTTON_COUNT> simulatorReleased{};
  std::array<bool, BUTTON_COUNT> simulatorHeld{};
  std::array<unsigned long, BUTTON_COUNT> simulatorPressStart{};

  bool simulatorPhysConfirmReleasePending = false;
  unsigned long simulatorPhysConfirmReleaseDuration = 0;
#endif

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  void updatePowerTapState();
  bool consumePowerConfirm();
  bool consumePowerBack();
};
