#pragma once

#include <cstddef>
#include <cstdint>

#include "Print.h"

class HalDisplay {
 public:
  enum RefreshMode {
    FULL_REFRESH,
    HALF_REFRESH,
    FAST_REFRESH,
  };

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }
};

extern HalDisplay display;
