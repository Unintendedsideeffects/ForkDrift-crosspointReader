#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Information about a single visible access point, as shown in the WiFi picker.
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi = 0;
  bool isEncrypted = false;
  bool hasSavedPassword = false;  // Whether we have saved credentials for this network
};

/**
 * Process-wide cache of the last WiFi scan.
 *
 * A full active scan costs ~4 s (13 channels x 300 ms), and the picker is
 * re-entered constantly — every network-backed activity (OTA, OPDS, KOReader
 * sync, Calibre, font download) launches it. Caching the result means a
 * re-entry within TTL_MS paints the list on the first frame instead of after a
 * multi-second "Scanning..." screen.
 *
 * Memory: entries are only held while fresh. isFresh() frees the vector the
 * moment the TTL lapses, so the ~1 KB of SSID strings is not resident between
 * uses.
 */
class WifiScanCache {
 public:
  static constexpr uint32_t TTL_MS = 30000;

  // Replaces the cache contents and restamps the freshness clock.
  static void store(std::vector<WifiNetworkInfo> networks);

  // True when a scan result is available and younger than TTL_MS. Frees the
  // cached entries as a side effect when the TTL has lapsed.
  static bool isFresh();

  static const std::vector<WifiNetworkInfo>& entries();

  // Drops the cache (e.g. after the radio was powered down by another owner,
  // so the previous result can no longer be trusted).
  static void invalidate();

  // Re-derives hasSavedPassword for every entry from WifiCredentialStore.
  // Call after adding or forgetting a credential so the cached list stays
  // consistent without forcing a rescan.
  static void refreshSavedFlags();

 private:
  static std::vector<WifiNetworkInfo> cached;
  static unsigned long storedAtMs;
  static bool hasResult;
};
