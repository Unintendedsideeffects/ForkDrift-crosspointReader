#include "BleCredentialParser.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>

namespace ble_credential_parser {
namespace {

constexpr size_t kMaxPayloadSize = 320;

std::string trim(const std::string& input) {
  auto start = std::find_if_not(input.begin(), input.end(), [](const unsigned char c) { return std::isspace(c); });
  auto end =
      std::find_if_not(input.rbegin(), input.rend(), [](const unsigned char c) { return std::isspace(c); }).base();
  if (start >= end) {
    return "";
  }
  return std::string(start, end);
}

bool parseJson(const std::string& payload, std::string& ssidOut, std::string& passwordOut) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    return false;
  }

  const char* ssid = doc["ssid"];
  if (!ssid || strlen(ssid) == 0) {
    return false;
  }

  const char* password = doc["password"] | "";
  ssidOut = ssid;
  passwordOut = password ? password : "";
  return true;
}

bool parseWifiQr(const std::string& payload, std::string& ssidOut, std::string& passwordOut) {
  if (payload.rfind("WIFI:", 0) != 0) {
    return false;
  }

  auto extractField = [&](const std::string& prefix) -> std::string {
    size_t start = payload.find(prefix);
    if (start == std::string::npos) return "";
    start += prefix.length();

    std::string result;
    for (size_t i = start; i < payload.length(); ++i) {
      char c = payload[i];
      if (c == '\\' && i + 1 < payload.length()) {
        result += payload[++i];
      } else if (c == ';') {
        break;
      } else {
        result += c;
      }
    }
    return result;
  };

  ssidOut = extractField("S:");
  passwordOut = extractField("P:");
  return !ssidOut.empty();
}

bool parseDelimited(const std::string& payload, std::string& ssidOut, std::string& passwordOut) {
  size_t delimiter = payload.find(',');
  if (delimiter == std::string::npos) {
    delimiter = payload.find('\n');
  }
  if (delimiter == std::string::npos) {
    return false;
  }

  ssidOut = trim(payload.substr(0, delimiter));
  passwordOut = trim(payload.substr(delimiter + 1));
  return !ssidOut.empty();
}

}  // namespace

bool parse(const std::string& payload, std::string& ssidOut, std::string& passwordOut) {
  if (payload.empty() || payload.size() > kMaxPayloadSize) {
    return false;
  }

  // Dispatch by prefix so parsers don't bleed into each other. A JSON payload
  // that has an invalid SSID should not fall through to the CSV parser and
  // "succeed" by treating the comma inside the JSON as a delimiter.
  if (payload[0] == '{') return parseJson(payload, ssidOut, passwordOut);
  if (payload.rfind("WIFI:", 0) == 0) return parseWifiQr(payload, ssidOut, passwordOut);
  return parseDelimited(payload, ssidOut, passwordOut);
}

}  // namespace ble_credential_parser
