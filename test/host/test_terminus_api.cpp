#include "doctest/doctest.h"
#include "util/TerminusApi.h"

TEST_CASE("Terminus base URL normalization migrates the retired cloud host") {
  CHECK(terminus_api::normalizeBaseUrl("") == terminus_api::kDefaultBaseUrl);
  CHECK(terminus_api::normalizeBaseUrl("https://api.trmnl.com") == terminus_api::kDefaultBaseUrl);
  CHECK(terminus_api::normalizeBaseUrl("https://trmnl.com/") == terminus_api::kDefaultBaseUrl);
  CHECK(terminus_api::normalizeBaseUrl("http://192.168.1.20:2300/") == "http://192.168.1.20:2300");
}

TEST_CASE("Terminus URL policy permits HTTPS and private LAN HTTP only") {
  CHECK(terminus_api::isAllowedRemoteUrl("https://trmnl.com/image.png"));
  CHECK(terminus_api::isAllowedRemoteUrl("http://192.168.1.20:2300/image.png"));
  CHECK(terminus_api::isAllowedRemoteUrl("http://10.1.2.3/image.bmp"));
  CHECK_FALSE(terminus_api::isAllowedRemoteUrl("http://example.com/image.png"));
  CHECK_FALSE(terminus_api::isAllowedRemoteUrl("http://terminus.local/image.png"));
  CHECK_FALSE(terminus_api::isAllowedRemoteUrl("file:///sleep/image.png"));
  CHECK_FALSE(terminus_api::isAllowedRemoteUrl("https://"));
  CHECK_FALSE(terminus_api::isAllowedRemoteUrl("https://bad host/image.png"));
}

TEST_CASE("Terminus manifest accepts the official string refresh rate") {
  terminus_api::DisplayManifest manifest;
  REQUIRE(terminus_api::parseDisplayManifest(R"({"image_url":"https://trmnl.com/screen.png","refresh_rate":"1800"})",
                                             manifest));
  CHECK(manifest.imageUrl == "https://trmnl.com/screen.png");
  CHECK(manifest.refreshIntervalS == 1800U);
}

TEST_CASE("Terminus manifest accepts BYOS numeric and omitted refresh rates") {
  terminus_api::DisplayManifest manifest;
  REQUIRE(terminus_api::parseDisplayManifest(
      R"({"image_url":"http://192.168.1.20:2300/screen.bmp","refresh_rate":3600})", manifest));
  CHECK(manifest.refreshIntervalS == 3600U);

  REQUIRE(terminus_api::parseDisplayManifest(R"({"image_url":"https://trmnl.com/screen.png"})", manifest));
  CHECK(manifest.refreshIntervalS == terminus_refresh::kDefaultIntervalS);
}

TEST_CASE("Terminus manifest bounds server cadence") {
  terminus_api::DisplayManifest manifest;
  REQUIRE(
      terminus_api::parseDisplayManifest(R"({"image_url":"https://trmnl.com/fast.png","refresh_rate":"0"})", manifest));
  CHECK(manifest.refreshIntervalS == terminus_refresh::kMinServerIntervalS);

  REQUIRE(terminus_api::parseDisplayManifest(R"({"image_url":"https://trmnl.com/slow.png","refresh_rate":100000})",
                                             manifest));
  CHECK(manifest.refreshIntervalS == terminus_refresh::kMaxServerIntervalS);
}

TEST_CASE("Terminus manifest rejects malformed or unsafe server data") {
  terminus_api::DisplayManifest manifest;
  CHECK_FALSE(terminus_api::parseDisplayManifest("not json", manifest));
  CHECK_FALSE(terminus_api::parseDisplayManifest(R"({"refresh_rate":900})", manifest));
  CHECK_FALSE(terminus_api::parseDisplayManifest(R"({"image_url":"http://example.com/screen.png","refresh_rate":900})",
                                                 manifest));
  CHECK_FALSE(terminus_api::parseDisplayManifest(R"({"image_url":"https://trmnl.com/screen.png","refresh_rate":"-1"})",
                                                 manifest));
  CHECK_FALSE(terminus_api::parseDisplayManifest(
      R"({"image_url":"https://trmnl.com/screen.png","refresh_rate":"900seconds"})", manifest));
  CHECK_FALSE(terminus_api::parseDisplayManifest(R"({"image_url":"https://trmnl.com/screen.png","refresh_rate":12.5})",
                                                 manifest));
}
