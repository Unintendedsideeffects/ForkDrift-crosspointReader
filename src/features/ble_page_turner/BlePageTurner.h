#pragma once

#if ENABLE_BLE_PAGE_TURNER && !defined(SIMULATOR)

#include <stdbool.h>

class BlePageTurner {
 public:
  BlePageTurner();
  ~BlePageTurner();

  // Initialize the Bluedroid stack, GAP, and HID over GATT profile
  void start();

  // Tear down the stack and free memory
  void stop();

  bool isConnected() const;

  // Send a Volume Down or Page Down keycode
  void sendPageNext();

  // Send a Volume Up or Page Up keycode
  void sendPagePrev();
};

#else
// Dummy implementation when feature is disabled
class BlePageTurner {
 public:
  void start() {}
  void stop() {}
  bool isConnected() const { return false; }
  void sendPageNext() {}
  void sendPagePrev() {}
};
#endif
