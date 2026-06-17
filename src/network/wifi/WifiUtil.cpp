#include "network/wifi/WifiUtil.h"

#include <WiFi.h>

bool hasStaWifiConnection() {
  const wifi_mode_t wifiMode = WiFi.getMode();
  if ((wifiMode & WIFI_MODE_STA) == 0) {
    return false;
  }
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}
