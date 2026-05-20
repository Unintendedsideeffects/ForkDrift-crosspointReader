#pragma once

constexpr int WL_CONNECTED = 3;

class WiFiClass {
 public:
  int status() const { return WL_CONNECTED; }
};

inline WiFiClass WiFi;
