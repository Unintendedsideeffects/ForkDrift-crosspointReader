#pragma once

#include <stdint.h>

#ifndef CROSSPOINT_BUILD_TIMESTAMP
#define CROSSPOINT_BUILD_TIMESTAMP 0UL
#endif

namespace crosspoint {

inline constexpr uint32_t buildTimestamp() { return static_cast<uint32_t>(CROSSPOINT_BUILD_TIMESTAMP); }

inline constexpr bool isBuildNewer(const uint32_t candidate, const uint32_t installed) { return candidate > installed; }

}  // namespace crosspoint
