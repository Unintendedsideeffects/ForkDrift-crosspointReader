#pragma once

#include <functional>
#include <initializer_list>

#include "MappedInputManager.h"

class ButtonNavigator final {
  using Callback = std::function<void()>;
  // Borrowed lists are consumed synchronously and never retained, so no
  // per-call heap vector: activities call onNext() etc. on every loop tick.
  using Buttons = std::initializer_list<MappedInputManager::Button>;

  const uint16_t continuousStartMs;
  const uint16_t continuousIntervalMs;
  uint32_t lastContinuousNavTime = 0;
  static MappedInputManager* mappedInput;

  [[nodiscard]] bool shouldNavigateContinuously() const;

 public:
  explicit ButtonNavigator(const uint16_t continuousIntervalMs = 500, const uint16_t continuousStartMs = 500)
      : continuousStartMs(continuousStartMs), continuousIntervalMs(continuousIntervalMs) {}

  static void setMappedInputManager(MappedInputManager& mappedInputManager) { mappedInput = &mappedInputManager; }

  void onNext(const Callback& callback);
  void onPrevious(const Callback& callback);
  void onPressAndContinuous(const Buttons& buttons, const Callback& callback);

  void onNextPress(const Callback& callback);
  void onPreviousPress(const Callback& callback);
  void onPress(const Buttons& buttons, const Callback& callback);

  void onNextRelease(const Callback& callback);
  void onPreviousRelease(const Callback& callback);
  void onRelease(const Buttons& buttons, const Callback& callback);

  void onNextContinuous(const Callback& callback);
  void onPreviousContinuous(const Callback& callback);
  void onContinuous(const Buttons& buttons, const Callback& callback);

  [[nodiscard]] static int nextIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousIndex(int currentIndex, int totalItems);

  [[nodiscard]] static int nextPageIndex(int currentIndex, int totalItems, int itemsPerPage);
  [[nodiscard]] static int previousPageIndex(int currentIndex, int totalItems, int itemsPerPage);

  // Static storage: an initializer_list returned from a braced list would dangle.
  [[nodiscard]] static Buttons getNextButtons() {
    static constexpr Buttons buttons = {MappedInputManager::Button::Down, MappedInputManager::Button::Right};
    return buttons;
  }
  [[nodiscard]] static Buttons getPreviousButtons() {
    static constexpr Buttons buttons = {MappedInputManager::Button::Up, MappedInputManager::Button::Left};
    return buttons;
  }
};
