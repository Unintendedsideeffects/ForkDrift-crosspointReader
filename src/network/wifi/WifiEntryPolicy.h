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
};

/**
 * Decides what the foreground WiFi picker should do when it opens.
 *
 * The ordering matters: adopting an existing link is checked before anything
 * else, because every network-backed activity re-enters this flow and a
 * re-association costs several seconds of user-visible latency for a link that
 * is already usable.
 *
 * Note that AdoptExistingLink is returned even when the background service is
 * running: the caller stops that service with keepWifi=true, which tears down
 * the HTTP server but leaves the association intact.
 */
EntryAction evaluateEntry(const EntryInput& input);

}  // namespace wifi_entry
