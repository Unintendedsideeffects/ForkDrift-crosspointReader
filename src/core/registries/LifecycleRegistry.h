#pragma once

#include <Logging.h>

#include <cstdint>

class GfxRenderer;

namespace core {

struct LifecycleEntry {
  void (*onStorageReady)();
  void (*onSettingsLoaded)(GfxRenderer& renderer);
  void (*onFontSetup)(GfxRenderer& renderer);
  void (*onFontFamilyChanged)(uint8_t newFontFamilyValue);
  void (*onWebSettingsApplied)();
  void (*onUploadCompleted)(const char* uploadPath, const char* uploadFileName);
  // Fired after STA association succeeds but before the route-heavy background
  // web server is allocated. Handlers may perform short, bounded network work
  // that needs more contiguous heap than remains once the server is running.
  void (*onBackgroundNetworkReady)();
  // Asked right after onBackgroundNetworkReady, and again on later attempts: does this
  // handler still need the server to stay down? Handlers that start asynchronous work in
  // onBackgroundNetworkReady report true until it finishes.
  //
  // This exists so the *dispatcher* chooses the waiting policy rather than the handler.
  // BackgroundWifiService runs on its own task and can afford to block; BackgroundWebServer
  // is driven from the main loop, where blocking freezes input and rendering. A handler
  // cannot tell which one is calling it, so it must not block either.
  bool (*backgroundStartupDeferred)();
  // Fired once each time a background web server transitions to RUNNING state —
  // either the on-charge/USB server or the WiFi one (BG_WIFI). It used to be
  // dispatched for the on-charge server ONLY, which meant a feature hooking it
  // never ran in Background Server = Always: the Terminus fetch was wired here
  // and silently never fired in the mode most users run.
  void (*onBackgroundServerStarted)();
  // Fired periodically while a background server is running. Edge hooks cannot
  // express a heartbeat: onBackgroundServerStarted fires once, so a server that
  // stays up at Home refreshes nothing afterwards. Handlers MUST gate themselves
  // on their own wall-clock interval (see util/WallClockInterval.h) — this fires
  // on a main-loop cadence, not at the handler's desired period.
  void (*onBackgroundServerTick)();
};

class LifecycleRegistry {
 public:
  static constexpr int kMaxEntries = 16;

  static void add(const LifecycleEntry& entry) {
    // cppcheck-suppress knownConditionTrueFalse
    if (count >= kMaxEntries) {
      LOG_ERR("REG", "LifecycleRegistry full (%d), entry dropped", kMaxEntries);
      return;
    }

    entries[count++] = entry;
  }

  static void dispatchStorageReady() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onStorageReady != nullptr) {
        entries[i].onStorageReady();
      }
    }
  }

  static void dispatchSettingsLoaded(GfxRenderer& renderer) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onSettingsLoaded != nullptr) {
        entries[i].onSettingsLoaded(renderer);
      }
    }
  }

  static void dispatchFontSetup(GfxRenderer& renderer) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onFontSetup != nullptr) {
        entries[i].onFontSetup(renderer);
      }
    }
  }

  static void dispatchFontFamilyChanged(uint8_t newValue) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onFontFamilyChanged != nullptr) {
        entries[i].onFontFamilyChanged(newValue);
      }
    }
  }

  static void dispatchWebSettingsApplied() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onWebSettingsApplied != nullptr) {
        entries[i].onWebSettingsApplied();
      }
    }
  }

  static void dispatchUploadCompleted(const char* uploadPath, const char* uploadFileName) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onUploadCompleted != nullptr) {
        entries[i].onUploadCompleted(uploadPath, uploadFileName);
      }
    }
  }

  static void dispatchBackgroundServerStarted() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onBackgroundServerStarted != nullptr) {
        entries[i].onBackgroundServerStarted();
      }
    }
  }

  static void dispatchBackgroundNetworkReady() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onBackgroundNetworkReady != nullptr) {
        entries[i].onBackgroundNetworkReady();
      }
    }
  }

  static bool anyBackgroundStartupDeferred() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].backgroundStartupDeferred != nullptr && entries[i].backgroundStartupDeferred()) {
        return true;
      }
    }
    return false;
  }

  static void dispatchBackgroundServerTick() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].onBackgroundServerTick != nullptr) {
        entries[i].onBackgroundServerTick();
      }
    }
  }

 private:
  inline static LifecycleEntry entries[kMaxEntries] = {};
  inline static int count = 0;
};

}  // namespace core
