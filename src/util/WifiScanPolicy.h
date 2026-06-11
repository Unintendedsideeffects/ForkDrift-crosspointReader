#pragma once

#include <WiFi.h>
#ifndef SIMULATOR
#include <esp_wifi.h>
#endif

inline void startWifiScanAsync() {
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
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true, /*passive=*/false, /*max_ms_per_chan=*/300);
}
