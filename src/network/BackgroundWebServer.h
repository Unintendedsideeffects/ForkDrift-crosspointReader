#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "util/WifiCredentialStore.h"

class CrossPointWebServer;

// TODO: Add configurable long-press button activation — hold a user-configured
// button for ~1s to start the background web server for ~1 minute, allowing the
// Android app to push ticket images and other data to the device.
class BackgroundWebServer {
 public:
  static BackgroundWebServer& getInstance();

  void loop(bool usbConnected, bool allowRun);
  bool isRunning() const;
  bool shouldPreventAutoSleep() const;
  bool wantsFastLoop() const;
  void invalidateCredentialsCache();
  void stop(bool keepWifi);

 private:
  BackgroundWebServer() = default;
  BackgroundWebServer(const BackgroundWebServer&) = delete;
  BackgroundWebServer& operator=(const BackgroundWebServer&) = delete;

  enum class State { IDLE, SCANNING, CONNECTING, RUNNING, WAIT_RETRY };

  void ensureCredentialsLoaded();
  void startScan();
  void startConnect(const std::string& ssid, const std::string& password);
  void startServer();
  void scheduleRetry(const char* reason);
  void stopAll(bool keepWifi = false);
  unsigned long computeBackoffMs() const;
  bool hasSessionExpired() const;
  void resetSession();

  State state = State::IDLE;
  unsigned long stateStartMs = 0;
  unsigned long nextRetryMs = 0;
  unsigned int retryAttempts = 0;
  unsigned long sessionStartMs = 0;
  bool sessionBlocked = false;
  bool mdnsStarted = false;
  bool allowRunCached = false;
  bool usbConnectedCached = false;
  bool lastAllowRunState = false;
  unsigned long allowRunStartMs = 0;
  bool credentialsLoaded = false;
  bool wifiOwned = false;

  std::vector<WifiCredential> credentials;
  std::string targetSsid;
  std::string targetPassword;

  std::unique_ptr<CrossPointWebServer> server;

  static constexpr unsigned long CONNECT_TIMEOUT_MS = 15000;
  static constexpr unsigned long SCAN_TIMEOUT_MS = 20000;
  static constexpr unsigned long RETRY_BASE_MS = 15000;
  static constexpr unsigned long RETRY_MAX_MS = 5UL * 60 * 1000;
  static constexpr unsigned long SERVER_WINDOW_MS = 2UL * 60 * 1000;
  static constexpr unsigned long SESSION_MAX_MS = 20UL * 60 * 1000;
  static constexpr unsigned long MIN_FREE_HEAP_BYTES = 60000;
  static constexpr unsigned long ALLOW_RUN_GRACE_MS = 500;
};
