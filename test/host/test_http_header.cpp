#include <string>

#include "HttpHeader.h"
#include "doctest/doctest.h"

TEST_CASE("parses trimmed name and value") {
  HttpHeader header;
  REQUIRE(parseHttpHeaderLine("  CF-Access-Client-Id :\t abc123  ", header));
  CHECK(header.name == "CF-Access-Client-Id");
  CHECK(header.value == "abc123");
}

TEST_CASE("splits on first colon only") {
  HttpHeader header;
  REQUIRE(parseHttpHeaderLine("X-Target: http://host:8080", header));
  CHECK(header.name == "X-Target");
  CHECK(header.value == "http://host:8080");
}

TEST_CASE("missing colon is a bare name and clears value") {
  HttpHeader header{"Old", "stale"};
  REQUIRE(parseHttpHeaderLine(" X-Flag ", header));
  CHECK(header.name == "X-Flag");
  CHECK(header.value == "");
}

TEST_CASE("blank line clears header") {
  HttpHeader header{"Old", "stale"};
  REQUIRE(parseHttpHeaderLine("   ", header));
  CHECK(header.name == "");
  CHECK(header.value == "");
}

TEST_CASE("formats and masks") {
  CHECK(formatHttpHeaderLine({"", "orphan"}) == "");
  CHECK(formatHttpHeaderLine({"X-Flag", ""}) == "X-Flag");
  CHECK(formatHttpHeaderLine({"X-Key", "secret"}) == "X-Key: secret");
  CHECK(formatHttpHeaderLine({"X-Key", "secret"}, true) == "X-Key: ******");
  CHECK(formatHttpHeaderLine({"X-Flag", ""}, true) == "X-Flag");
}

TEST_CASE("format round-trips through parse") {
  const HttpHeader original{"X-Proxy-Token", "Bearer a:b"};
  HttpHeader parsed;
  REQUIRE(parseHttpHeaderLine(formatHttpHeaderLine(original), parsed));
  CHECK(parsed.name == original.name);
  CHECK(parsed.value == original.value);
}

TEST_CASE("rejects CR and LF injection") {
  HttpHeader header{"keep", "me"};
  CHECK_FALSE(parseHttpHeaderLine("X-Bad: one\r\nX-Injected: two", header));
  CHECK(header.name == "keep");
  CHECK(header.value == "me");
  CHECK_FALSE(parseHttpHeaderLine("X-Bad\nName: value", header));
}

TEST_CASE("rejects reserved names") {
  HttpHeader header;
  CHECK_FALSE(parseHttpHeaderLine("Authorization: Basic abc", header));
  CHECK_FALSE(parseHttpHeaderLine("x-auth-user: alice", header));
  CHECK_FALSE(parseHttpHeaderLine("Host: evil.example", header));
}

TEST_CASE("rejects a 160-byte overflow") {
  HttpHeader header;
  const std::string tooLong = "X-" + std::string(160, 'a') + ": b";
  CHECK_FALSE(parseHttpHeaderLine(tooLong, header));
}

TEST_CASE("empty line is a clear not an invalid header") {
  CHECK(isValidHttpHeader("", ""));
  CHECK_FALSE(isValidHttpHeader("", "value"));
}
