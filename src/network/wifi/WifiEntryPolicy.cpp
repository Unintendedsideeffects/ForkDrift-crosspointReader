#include "network/wifi/WifiEntryPolicy.h"

namespace wifi_entry {

EntryAction evaluateEntry(const EntryInput& input) {
  // Release first, always. The background service owns port 80 and a
  // request-handling task; the caller stops it with keepWifi=true so the radio
  // association survives and can be adopted on the next evaluation.
  if (input.backgroundServiceRunning && !input.backgroundReleaseFailed) {
    return EntryAction::ReleaseBackgroundService;
  }

  // A usable link wins over everything else — no scan, no re-association, no
  // DHCP round trip. Gated on allowAutoConnect: callers that pass
  // autoConnect=false (Settings > WiFi setup, OPDS server setup) opened this
  // screen to *change* networks, so silently adopting would defeat the intent.
  if (input.linkUp && input.allowAutoConnect) {
    return EntryAction::AdoptExistingLink;
  }

  if (!input.linkUp && input.allowAutoConnect && input.hasLastCredential) {
    return EntryAction::AutoConnectLast;
  }

  if (input.scanCacheFresh) {
    return EntryAction::ShowCachedNetworks;
  }

  return EntryAction::Scan;
}

ScanRetryDecision evaluateScanRetry(const ScanRetryInput& input) {
  if (input.failureCount == 0 || input.failureCount > input.maxRetries) {
    return ScanRetryDecision{};
  }

  // Exponential: base, 2x, 4x... A scan aborted by an in-flight auto-connect
  // needs the radio to go quiet, and how long that takes varies with the
  // association. Shifting by (failureCount - 1) is safe for any plausible
  // retry budget; clamp anyway so a mis-set maxRetries cannot shift past the
  // width of the type.
  constexpr uint8_t kMaxShift = 8;
  const uint8_t shift = input.failureCount - 1 < kMaxShift ? input.failureCount - 1 : kMaxShift;
  return ScanRetryDecision{true, input.baseDelayMs << shift};
}

}  // namespace wifi_entry
