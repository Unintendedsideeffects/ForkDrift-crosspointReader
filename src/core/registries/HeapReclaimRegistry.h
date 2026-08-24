#pragma once

#include <Logging.h>

namespace core {

// A cache the firmware holds purely for speed and can rebuild from the SD card.
// Registering one lets code that needs heap reclaim it instead of failing: the
// owner keeps deciding *how* to release, the requester only asks.
//
// release() takes no context on purpose. Every entry must act on static or
// singleton state, never on an instance — activities are heap-allocated and
// deleted on exit (see main.cpp exitActivity()), so an instance pointer parked
// in a static registry would dangle the moment the user left the screen. With no
// context there is nothing to dangle and nothing to deregister.
//
// release() must also be idempotent (safe when nothing is held) and must leave
// the owner able to rebuild on its next render — a reclaim is a cache eviction,
// not a teardown.
//
// THREADING — read before adding an entry or a call site.
//
// Entries free buffers that the RENDER TASK reads (rendering runs on its own
// FreeRTOS task; see ActivityManager's "ActivityManagerRender"). Handlers that
// free render-visible memory therefore take a RenderLock themselves, which makes
// them safe for any caller EXCEPT one that already holds that lock: RenderLock
// wraps a non-recursive mutex taken with portMAX_DELAY, so re-taking it on the
// same task hangs the device outright.
//
// So: never call releaseAll() from inside a RenderLock scope, and never from the
// render task. Today the only caller is BackgroundWifiService::canStartNow() on
// the main loop, which holds no lock.
struct HeapReclaimEntry {
  const char* name = nullptr;
  void (*release)() = nullptr;
};

class HeapReclaimRegistry {
 public:
  static constexpr int kMaxEntries = 4;

  static void add(const HeapReclaimEntry& entry) {
    // cppcheck-suppress knownConditionTrueFalse
    if (count >= kMaxEntries) {
      LOG_ERR("REG", "HeapReclaimRegistry full (%d), entry dropped", kMaxEntries);
      return;
    }

    entries[count++] = entry;
  }

  // Releases every registered cache. Returns nothing deliberately: the caller
  // must re-read the heap itself, because what decides success is the resulting
  // free AND largest-contiguous figures, not the sum of what each owner believes
  // it dropped. Freeing 48 KB in two fragments is not the same as one run.
  static void releaseAll() {
    for (int i = 0; i < count; ++i) {
      if (entries[i].release == nullptr) {
        continue;
      }
      entries[i].release();
      LOG_DBG("REG", "heap reclaim: released %s", entries[i].name != nullptr ? entries[i].name : "?");
    }
  }

  // The suppression is for cppcheck's cross-TU blind spot: registrations happen
  // in feature/activity TUs (e.g. HomeActivity), so from this TU count looks
  // always-zero. BackgroundWifiService's !empty() check carries the twin.
  static bool empty() {
    // cppcheck-suppress knownConditionTrueFalse
    return count == 0;
  }

 private:
  inline static HeapReclaimEntry entries[kMaxEntries] = {};
  inline static int count = 0;
};

}  // namespace core
