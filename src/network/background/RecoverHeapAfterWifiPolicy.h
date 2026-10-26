#pragma once

#include <cstdint>

namespace wifi_heap_recovery {

enum class Action : uint8_t {
  Noop,
  LeaveWifiForSession,
  TearDownWifi,
};

struct Input {
  bool wifiOff = false;
  bool sessionKeepsWifi = false;
};

constexpr Action evaluate(const Input& input) {
  if (input.wifiOff) {
    return Action::Noop;
  }
  if (input.sessionKeepsWifi) {
    return Action::LeaveWifiForSession;
  }
  return Action::TearDownWifi;
}

}  // namespace wifi_heap_recovery
