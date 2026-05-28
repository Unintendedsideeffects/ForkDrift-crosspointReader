#pragma once

#include <HalGPIO.h>

#ifdef SIMULATOR
#include <array>
#endif

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::PageForward) + 1;

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() { gpio.update(); }
  void setReaderMode(bool enabled) { readerMode = enabled; }
  void suppressNextBackRelease() { suppressBackRelease = true; }
  bool wasPressed(Button button);
  bool wasReleased(Button button);
  bool isPressed(Button button) const;
  void clearTransientState();
  // Inject a one-frame virtual activation for the given button.
  // Both wasPressed() and wasReleased() will fire for it on the next check.
  // Call from the main-loop task only (virtualActivatedMask is not thread-safe).
  void injectVirtualActivation(Button button);
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

#ifdef SIMULATOR
  void simulatorInjectPress(Button button);
  void simulatorInjectRelease(Button button);
  void simulatorClearInputFrame();
#endif

 private:
  HalGPIO& gpio;
  bool readerMode = false;
  mutable bool suppressBackRelease = false;
  unsigned long pendingPowerReleaseMs = 0;
  unsigned long doubleTapReadyMs = 0;
  bool pendingPowerRelease = false;
  bool doubleTapReady = false;
  bool powerReleaseConsumed = false;

#ifdef SIMULATOR
  std::array<bool, BUTTON_COUNT> simulatorPressed{};
  std::array<bool, BUTTON_COUNT> simulatorReleased{};
  std::array<bool, BUTTON_COUNT> simulatorHeld{};
  std::array<unsigned long, BUTTON_COUNT> simulatorPressStart{};
#endif

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  void updatePowerTapState();
  bool consumePowerConfirm();
  bool consumePowerBack();
};
