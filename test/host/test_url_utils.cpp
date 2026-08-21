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

TEST_CASE("UrlUtils: encodeUnsafeUrlChars") {
  SUBCASE("safe url unchanged") {
    CHECK(encodeUnsafeUrlChars("http://example.com/books/page?id=123&sort=asc") ==
          "http://example.com/books/page?id=123&sort=asc");
  }

  SUBCASE("spaces and unsafe ASCII characters encoded") {
    CHECK(encodeUnsafeUrlChars("http://example.com/book name with spaces.epub") ==
          "http://example.com/book%20name%20with%20spaces.epub");
    CHECK(encodeUnsafeUrlChars("http://example.com/{path}/<tag>?q=`test`|foo^bar\\baz") ==
          "http://example.com/%7Bpath%7D/%3Ctag%3E?q=%60test%60%7Cfoo%5Ebar%5Cbaz");
  }

  SUBCASE("non-ascii utf-8 characters encoded") {
    // Non-ASCII byte 0xE9 (é)
    CHECK(encodeUnsafeUrlChars("http://example.com/café") == "http://example.com/caf%C3%A9");
  }

  SUBCASE("preserves already-valid %HH percent encodings without double-encoding") {
    CHECK(encodeUnsafeUrlChars("http://example.com/path%20with%2Fexisting%3Fencodings") ==
          "http://example.com/path%20with%2Fexisting%3Fencodings");
    CHECK(encodeUnsafeUrlChars("http://example.com/%E2%80%99%20mixed with raw spaces") ==
          "http://example.com/%E2%80%99%20mixed%20with%20raw%20spaces");
    CHECK(encodeUnsafeUrlChars("http://example.com/lower%2a%3b%4f") ==
          "http://example.com/lower%2a%3b%4f");
  }

  SUBCASE("lone or invalid % is safely percent-encoded") {
    // Lone % at end of string
    CHECK(encodeUnsafeUrlChars("http://example.com/100%") == "http://example.com/100%25");
    // Invalid hex digits after %
    CHECK(encodeUnsafeUrlChars("http://example.com/%zz") == "http://example.com/%25zz");
    // Short percent sequence at end
    CHECK(encodeUnsafeUrlChars("http://example.com/%2") == "http://example.com/%252");
  }

  SUBCASE("buildUrl encodes unsafe characters while preserving valid %HH") {
    CHECK(buildUrl("http://example.com/base", "book with spaces.epub") ==
          "http://example.com/base/book%20with%20spaces.epub");
    CHECK(buildUrl("http://example.com", "/download?file=my%20book.epub&other=a b") ==
          "http://example.com/download?file=my%20book.epub&other=a%20b");
  }
}

