#pragma once

namespace SpiBusMutex {

inline void lock() {}
inline void unlock() {}

struct Guard {
  Guard() = default;
  ~Guard() = default;
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;
};

}  // namespace SpiBusMutex
