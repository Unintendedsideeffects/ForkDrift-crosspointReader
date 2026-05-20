#pragma once

#include <cstdint>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6

#define SPI_MISO 7

class HalGPIO {
 public:
  enum class DeviceType : uint8_t { X4, X3 };
  enum class WakeupReason : uint8_t { PowerButton, AfterFlash, AfterUSBPower, Other };

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;

  void begin() {}
  void update() {}

  bool wasPressed(uint8_t /*button*/) const { return false; }
  bool wasReleased(uint8_t /*button*/) const { return false; }
  bool isPressed(uint8_t /*button*/) const { return false; }
  bool wasAnyPressed() const { return false; }
  bool wasAnyReleased() const { return false; }
  unsigned long getHeldTime() const { return 0; }
  bool deviceIsX3() const { return false; }
  bool deviceIsX4() const { return true; }
  bool isUsbConnected() const { return false; }
  WakeupReason getWakeupReason() const { return WakeupReason::Other; }
};

extern HalGPIO gpio;
