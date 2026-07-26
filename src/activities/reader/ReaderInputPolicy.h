#pragma once

struct PhysicalConfirmRelease {
  bool active;
  unsigned long durationMs;
};

class PhysicalConfirmTracker {
 public:
  void update(bool isPressed, unsigned long now) {
    releasedThisFrame = false;
    consumed = false;

    if (isPressed) {
      if (!wasPressed) {
        pressStartMs = now;
        wasPressed = true;
      }
    } else {
      if (wasPressed) {
        releasedThisFrame = true;
        releaseDurationMs = now - pressStartMs;
        wasPressed = false;
      }
    }
  }

  PhysicalConfirmRelease peekRelease() const {
    if (releasedThisFrame && !consumed) {
      return {true, releaseDurationMs};
    }
    return {false, 0};
  }

  void consumeRelease() {
    if (releasedThisFrame) {
      consumed = true;
    }
  }

  bool isConsumed() const { return consumed; }

  void clear() {
    wasPressed = false;
    pressStartMs = 0;
    releasedThisFrame = false;
    releaseDurationMs = 0;
    consumed = false;
  }

#ifdef SIMULATOR
  void simulatorInjectRelease(unsigned long durationMs) {
    releasedThisFrame = true;
    releaseDurationMs = durationMs;
    wasPressed = false;
    consumed = false;
  }
#endif

 private:
  bool wasPressed = false;
  unsigned long pressStartMs = 0;
  bool releasedThisFrame = false;
  unsigned long releaseDurationMs = 0;
  bool consumed = false;
};

struct ReaderInputPolicy {
  static bool resolveDualSideRightRelease(bool rawConfirmReleased, bool rawRightReleased,
                                          const PhysicalConfirmTracker& tracker) {
    if (tracker.peekRelease().active) {
      return true;
    }
    if (rawConfirmReleased && tracker.isConsumed()) {
      return rawRightReleased;
    }
    return rawConfirmReleased || rawRightReleased;
  }
};
