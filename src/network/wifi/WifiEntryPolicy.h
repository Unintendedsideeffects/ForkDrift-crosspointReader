#pragma once

#include <cstdint>

namespace wifi_entry {

enum class EntryAction : uint8_t {
  // A usable STA link already exists — hand it straight to the caller. No
  // scan, no re-association, no DHCP round trip.
  AdoptExistingLink,
  // The background service owns the radio. Release it (keeping the link up)
  // and re-evaluate on the next tick.
  ReleaseBackgroundService,
  // No link, but we have credentials for the last-used network.
  AutoConnectLast,
  // No link; a recent scan result is still valid, so paint it immediately.
  ShowCachedNetworks,
  // No link and nothing cached — run a fresh scan.
  Scan,
};

struct EntryInput {
  // hasStaWifiConnection(): associated, and DHCP has produced an address.
  bool linkUp = false;
  // BackgroundWifiService::isRunning()
  bool backgroundServiceRunning = false;
  // Caller opted into reconnecting to the last network without a picker.
  bool allowAutoConnect = false;
  // A credential exists for WIFI_STORE.getLastConnectedSsid().
  bool hasLastCredential = false;
  // WifiScanCache::isFresh()
  bool scanCacheFresh = false;
  // backgroundReleaseFailed: Set once the caller has issued a release that did not take effect (the
  // service is still running after stop() returned). The service declines to
  // force-delete a task holding a mutex, so retrying can never succeed — the
  // picker proceeds without the radio handoff rather than blocking forever.
  bool backgroundReleaseFailed = false;
};

/**
 * Decides what the foreground WiFi picker should do when it opens.
 *
 * 1. Release first. When the background service is running, it owns port 80
 *    and a request-handling task, so it is released prior to other checks. The
 *    caller stops it with keepWifi=true so the association survives and can
 *    be adopted on the next evaluation. Once a release has been issued that
 *    did not take effect, the policy stops asking and falls through, because
 *    the service declines to force-delete a task holding a mutex and a retry
 *    can never succeed.
 * 2. Adopt a usable link — but only when allowAutoConnect. Every
 *    network-backed activity re-enters this flow and a re-association costs
 *    seconds of user-visible latency. Callers that pass autoConnect=false
 *    (Settings -> WiFi setup, OPDS server setup) opened the screen to change
 *    networks, so adopting silently would defeat the user's intent.
 * 3. Auto-connect to the last network when there is no link, allowAutoConnect
 *    is set, and a credential exists.
 * 4. Show the cached scan when it is still fresh, else scan.
 */
EntryAction evaluateEntry(const EntryInput& input);

struct ScanRetryInput {
  // Failures observed so far, including the one being handled (first = 1).
  uint8_t failureCount = 0;
  uint8_t maxRetries = 3;
  // Delay before the first retry; each further retry doubles it.
  unsigned long baseDelayMs = 0;
};

struct ScanRetryDecision {
  // False once the budget is spent — the caller shows whatever it has.
  bool retry = false;
  // How long to let the radio settle before re-arming and rescanning.
  unsigned long delayMs = 0;
};

/**
 * Decides whether a failed WiFi scan is retried, and how long to wait first.
 *
 * The delay is the whole point. The first scan after STA power-up is aborted
 * by the SDK's in-flight NVS auto-connect, which runs for hundreds of
 * milliseconds. Retrying in the same loop() tick — as this code did until the
 * backoff was added — burns the entire retry budget in single-digit
 * milliseconds, always while the radio is still busy, so every attempt fails
 * for the same reason and the user gets an empty network list. Each retry must
 * re-arm the radio and then wait, and the wait must grow: a fixed short delay
 * loses the same race on a slow association.
 */
ScanRetryDecision evaluateScanRetry(const ScanRetryInput& input);

}  // namespace wifi_entry
