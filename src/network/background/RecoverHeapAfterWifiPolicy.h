#pragma once

#include <cstdint>

namespace wifi_heap_recovery {

enum class Action : uint8_t {
  Noop,
  LeaveWifiForAlways,
  TearDownWifi,
};

struct Input {
  bool wifiOff = false;
  bool alwaysKeepsWifi = false;
};

constexpr Action evaluate(const Input& input) {
  if (input.wifiOff) {
    return Action::Noop;
  }
  if (input.alwaysKeepsWifi) {
    return Action::LeaveWifiForAlways;
  }
  return Action::TearDownWifi;
}

}  // namespace wifi_heap_recovery
