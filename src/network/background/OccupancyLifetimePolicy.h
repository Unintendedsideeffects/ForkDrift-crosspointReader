#pragma once

#include <cstdint>

namespace occupancy {

enum class AlwaysStop : uint8_t {
  LeaveRunning,
  StopKeepWifi,
};

struct AlwaysStopInput {
  bool occupiesReaderHeap = false;
  bool occupiesForegroundHttp = false;
  bool rebuildingSettingsLists = false;
};

constexpr AlwaysStop evaluateAlwaysStop(const AlwaysStopInput& input) {
  if (input.occupiesReaderHeap || input.occupiesForegroundHttp) {
    return AlwaysStop::StopKeepWifi;
  }
  return AlwaysStop::LeaveRunning;
}

enum class ReaderExitReclaim : uint8_t {
  ReleaseInflateWindow,
  KeepInflateWindow,
};

constexpr ReaderExitReclaim evaluateReaderExitReclaim(const bool inflateWindowInUse) {
  return inflateWindowInUse ? ReaderExitReclaim::KeepInflateWindow : ReaderExitReclaim::ReleaseInflateWindow;
}

}  // namespace occupancy
