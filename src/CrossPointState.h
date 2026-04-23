#pragma once
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <string>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
  // Set by USB/HTTP open_book command; drained by main loop to navigate to the book.
  // Not persisted — cleared on every boot.
  std::string pendingOpenPath;
  // Remote page turn: +1 = forward, -1 = back, 0 = none.
  // Written by WiFi/USB handler tasks, read and cleared by main loop.
  // volatile guarantees the main loop re-reads after a task switch.
  volatile int8_t pendingPageTurn = 0;
  // Remote screenshot trigger. Written by WiFi handler task, read and cleared by main loop.
  std::atomic<bool> pendingScreenshot{false};
  uint8_t lastSleepImage = UINT8_MAX;  // UINT8_MAX = unset sentinel
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  // Exponential backoff for WiFi auto-connect.
  // skipCount: remaining wakes to skip before next attempt (decremented each wake).
  // backoffLevel: exponent for next skip window; skip = (1 << backoffLevel) - 1.
  // Reset both to 0 when a successful API call is observed.
  uint8_t wifiAutoConnectSkipCount = 0;
  uint8_t wifiAutoConnectBackoffLevel = 0;
  bool wifiAutoConnectWaitingForNewCredential = false;
  // Set by reader activities on exit so HomeActivity knows to do a full e-ink
  // refresh on its first render. Not persisted — runtime only.
  bool pendingHomeFullRefresh = false;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
