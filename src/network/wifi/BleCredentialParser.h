#pragma once

#include <string>

namespace ble_credential_parser {

// Parses a BLE WiFi provisioning payload. Accepted formats:
//   JSON:     {"ssid":"MyWiFi","password":"secret"}
//   WiFi QR:  WIFI:T:WPA;S:MyWiFi;P:secret;;
//   CSV:      ssid,password
//   Two-line: ssid\npassword
//
// Returns true and sets ssidOut/passwordOut on success.
// passwordOut may be empty for open networks.
bool parse(const std::string& payload, std::string& ssidOut, std::string& passwordOut);

}  // namespace ble_credential_parser
