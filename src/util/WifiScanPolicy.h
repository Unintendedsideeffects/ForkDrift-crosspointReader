#pragma once

#include <WiFi.h>
#ifndef SIMULATOR
#include <esp_wifi.h>
#endif

// Returns WiFi.scanNetworks()'s result so callers can distinguish the three
// outcomes that matter: a scan was started (>= 0), one was ALREADY running and
// this call did nothing (WIFI_SCAN_RUNNING), or it could not start
// (WIFI_SCAN_FAILED). Discarding this is how a caller ends up polling
// scanComplete() for a scan it never launched — which reports WIFI_SCAN_FAILED
// and looks identical to a real failure.
inline int16_t startWifiScanAsync() {
#ifndef SIMULATOR
  // World-safe 1-13 with AUTO policy: follows the AP's country once associated,
  // and keeps EU channels 12/13 visible while unassociated (the default mask
  // hid networks on those channels). Idempotent; WiFi is initialized at every
  // call site before this runs.
  const wifi_country_t country = {
      .cc = "01",
      .schan = 1,
      .nchan = 13,
      .policy = WIFI_COUNTRY_POLICY_AUTO,
  };
  esp_wifi_set_country(&country);
#endif

  // Include hidden networks so non-broadcast SSIDs are not silently dropped.
  //
  // max_ms_per_chan also sets the framework's scan watchdog: WiFiScan.cpp does
  // `_scanTimeout = max_ms_per_chan * 20`, and scanComplete() returns
  // WIFI_SCAN_FAILED once that elapses. At 300 that watchdog was 6,000 ms while
  // a 13-channel active scan needs 13 * 300 = 3,900 ms plus per-channel
  // overhead — close enough that on device every scan was being declared failed
  // at exactly 6.00 s. 500 gives a 10,000 ms watchdog against ~6,500 ms of
  // scanning, restoring real headroom. Raising this LENGTHENS the allowance;
  // do not "optimise" it downwards without re-measuring on hardware.
  return WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true, /*passive=*/false, /*max_ms_per_chan=*/500);
}
