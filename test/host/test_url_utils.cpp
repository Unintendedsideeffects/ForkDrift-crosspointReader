#include "doctest/doctest.h"
#include "src/util/UrlUtils.h"

using namespace UrlUtils;

TEST_CASE("UrlUtils: isHttpsUrl") {
  CHECK(isHttpsUrl("https://x"));
  CHECK_FALSE(isHttpsUrl("http://x"));
  CHECK_FALSE(isHttpsUrl(""));
}

TEST_CASE("UrlUtils: isPrivateLanHttpUrl TRUE") {
  CHECK(isPrivateLanHttpUrl("http://10.0.0.5"));
  CHECK(isPrivateLanHttpUrl("http://192.168.1.10/path"));
  CHECK(isPrivateLanHttpUrl("http://127.0.0.1:8080"));
  CHECK(isPrivateLanHttpUrl("http://172.16.0.1"));
  CHECK(isPrivateLanHttpUrl("http://172.31.255.255"));
}

TEST_CASE("UrlUtils: isPrivateLanHttpUrl FALSE") {
  CHECK_FALSE(isPrivateLanHttpUrl("http://8.8.8.8"));
  CHECK_FALSE(isPrivateLanHttpUrl("http://172.15.0.1"));
  CHECK_FALSE(isPrivateLanHttpUrl("http://172.32.0.1"));
  CHECK_FALSE(isPrivateLanHttpUrl("http://example.com"));
  CHECK_FALSE(isPrivateLanHttpUrl("https://10.0.0.5"));
  CHECK_FALSE(isPrivateLanHttpUrl("http://"));
  CHECK_FALSE(isPrivateLanHttpUrl("http://1.2.3.4.evil.com"));
}

TEST_CASE("UrlUtils: ensureProtocol") {
  CHECK(ensureProtocol("example.com") == "http://example.com");
  CHECK(ensureProtocol("http://x") == "http://x");
  CHECK(ensureProtocol("https://x") == "https://x");
}

TEST_CASE("UrlUtils: extractHost") {
  CHECK(extractHost("http://example.com/path") == "http://example.com");
  CHECK(extractHost("example.com/path") == "example.com");
  CHECK(extractHost("http://example.com") == "http://example.com");
}

TEST_CASE("UrlUtils: buildUrl") {
  // absolute URL passthrough
  CHECK(buildUrl("http://h", "https://other/x") == "https://other/x");
  // absolute path
  CHECK(buildUrl("http://h.com/base", "/feed") == "http://h.com/feed");
  // relative path
  CHECK(buildUrl("http://h.com/base", "feed") == "http://h.com/base/feed");
  // empty path
  CHECK(buildUrl("h.com", "") == "http://h.com");
  // query stripped on relative join
  CHECK(buildUrl("http://h.com/base?q=1", "feed") == "http://h.com/base/feed");
  // trailing slash base
  CHECK(buildUrl("http://h.com/base/", "feed") == "http://h.com/base/feed");
}
