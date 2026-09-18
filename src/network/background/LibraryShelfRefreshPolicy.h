#pragma once

#include <cstdint>

// Library shelf is a streaming OPDS fetch of at most six books. The old 84 KB
// floor never admitted measured Always-idle Home (~37–42 KB). Admit at
// HttpDownloader::MIN_HEAP_FOR_HTTPS (38 KB). If HTTP is resident and heap is
// short, pause the server with keep-Wi-Fi rather than skip forever.

namespace library_shelf {

enum class RefreshAction : uint8_t {
  SkipLuxury,
  PauseHttpThenRetry,
  Refresh,
};

struct RefreshInput {
  uint32_t freeBytes = 0;
  bool httpServerResident = false;
};

constexpr uint32_t kMinFreeBytes = 38000;
constexpr uint32_t kAuthBackoffSeconds = 15UL * 60UL;

constexpr RefreshAction evaluate(const RefreshInput& input) {
  if (input.freeBytes >= kMinFreeBytes) {
    return RefreshAction::Refresh;
  }
  if (input.httpServerResident) {
    return RefreshAction::PauseHttpThenRetry;
  }
  return RefreshAction::SkipLuxury;
}

constexpr bool skipAfterAuthFailure(const long nowEpoch, const long lastAuthFailureEpoch, const bool clockUsable) {
  if (lastAuthFailureEpoch == 0) {
    return false;
  }
  if (!clockUsable) {
    return true;
  }
  if (nowEpoch < lastAuthFailureEpoch) {
    return false;
  }
  return (nowEpoch - lastAuthFailureEpoch) < static_cast<long>(kAuthBackoffSeconds);
}

}  // namespace library_shelf
