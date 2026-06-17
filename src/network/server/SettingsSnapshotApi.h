#pragma once

#include <Arduino.h>

class CrossPointSettings;

namespace network {

String buildSettingsSnapshotJson(const CrossPointSettings& settings);

}  // namespace network
