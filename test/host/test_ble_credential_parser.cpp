#include <string>

#include "doctest/doctest.h"
#include "network/wifi/BleCredentialParser.h"

namespace {

bool parse(const std::string& payload, std::string& ssid, std::string& password) {
  return ble_credential_parser::parse(payload, ssid, password);
}

}  // namespace

TEST_CASE("BleCredentialParser - JSON format") {
  std::string ssid, password;

  SUBCASE("valid JSON with password") {
    CHECK(parse(R"({"ssid":"MyWiFi","password":"secret"})", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "secret");
  }

  SUBCASE("valid JSON without password field") {
    CHECK(parse(R"({"ssid":"OpenNet"})", ssid, password));
    CHECK(ssid == "OpenNet");
    CHECK(password == "");
  }

  SUBCASE("valid JSON with empty password") {
    CHECK(parse(R"({"ssid":"OpenNet","password":""})", ssid, password));
    CHECK(ssid == "OpenNet");
    CHECK(password == "");
  }

  SUBCASE("missing ssid field") { CHECK_FALSE(parse(R"({"password":"secret"})", ssid, password)); }

  SUBCASE("empty ssid field") { CHECK_FALSE(parse(R"({"ssid":"","password":"secret"})", ssid, password)); }

  SUBCASE("non-string password is ignored gracefully") {
    CHECK(parse(R"({"ssid":"MyWiFi","password":42})", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "");
  }
}

TEST_CASE("BleCredentialParser - WiFi QR format") {
  std::string ssid, password;

  SUBCASE("full WPA QR string") {
    CHECK(parse("WIFI:T:WPA;S:MyWiFi;P:secret;;", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "secret");
  }

  SUBCASE("open network (no P field)") {
    CHECK(parse("WIFI:T:nopass;S:OpenNet;;", ssid, password));
    CHECK(ssid == "OpenNet");
    CHECK(password == "");
  }

  SUBCASE("escaped semicolon in password") {
    CHECK(parse("WIFI:T:WPA;S:MyNet;P:pass\\;word;;", ssid, password));
    CHECK(ssid == "MyNet");
    CHECK(password == "pass;word");
  }

  SUBCASE("missing WIFI: prefix is not QR") {
    // Falls through to delimited parser, which also fails for this input
    CHECK_FALSE(parse("S:MyWiFi;P:secret;;", ssid, password));
  }
}

TEST_CASE("BleCredentialParser - delimited formats") {
  std::string ssid, password;

  SUBCASE("comma-separated") {
    CHECK(parse("MyWiFi,secret", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "secret");
  }

  SUBCASE("newline-separated") {
    CHECK(parse("MyWiFi\nsecret", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "secret");
  }

  SUBCASE("comma-separated with whitespace trimmed") {
    CHECK(parse("  MyWiFi  ,  secret  ", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "secret");
  }

  SUBCASE("open network via comma (empty password)") {
    CHECK(parse("MyWiFi,", ssid, password));
    CHECK(ssid == "MyWiFi");
    CHECK(password == "");
  }

  SUBCASE("no delimiter returns false") { CHECK_FALSE(parse("justanssid", ssid, password)); }
}

TEST_CASE("BleCredentialParser - payload validation") {
  std::string ssid, password;

  SUBCASE("empty payload") { CHECK_FALSE(parse("", ssid, password)); }

  SUBCASE("payload over 320 bytes") {
    std::string huge(321, 'a');
    CHECK_FALSE(parse(huge, ssid, password));
  }

  SUBCASE("321-byte payload is rejected") {
    std::string payload(321, 'x');
    CHECK_FALSE(parse(payload, ssid, password));
  }
}
