#include "doctest/doctest.h"
#include "network/http/HttpOrigin.h"

// Credentials must only follow a redirect to the exact origin they were configured
// for (upstream d6f7f2565); these pin the origin comparison the redirect guard uses.
TEST_CASE("http_origin parses scheme, host and port") {
  http_origin::Origin o;
  REQUIRE(http_origin::parse("https://Books.Example.org/opds?x=1", o));
  CHECK(o.https);
  CHECK(o.host == "Books.Example.org");
  CHECK(o.port == 443);

  REQUIRE(http_origin::parse("http://user:pw@192.168.86.25:8085/api/v1/opds", o));
  CHECK_FALSE(o.https);
  CHECK(o.host == "192.168.86.25");
  CHECK(o.port == 8085);

  CHECK_FALSE(http_origin::parse("/relative/path", o));
  CHECK_FALSE(http_origin::parse("ftp://host/file", o));
  CHECK_FALSE(http_origin::parse("http://host:99999/", o));
  CHECK_FALSE(http_origin::parse("http://:80/", o));
}

TEST_CASE("http_origin keeps credentials on the same origin only") {
  http_origin::Origin cred, hop;
  REQUIRE(http_origin::parse("https://opds.example.org/catalog", cred));

  REQUIRE(http_origin::parse("https://OPDS.example.org:443/download/1.epub", hop));
  CHECK(http_origin::sameOrigin(cred, hop));  // case and default port do not matter

  REQUIRE(http_origin::parse("https://cdn.example.net/1.epub", hop));
  CHECK_FALSE(http_origin::sameOrigin(cred, hop));  // other host
  REQUIRE(http_origin::parse("https://opds.example.org:8443/1.epub", hop));
  CHECK_FALSE(http_origin::sameOrigin(cred, hop));  // other port
  REQUIRE(http_origin::parse("http://opds.example.org/1.epub", hop));
  CHECK_FALSE(http_origin::sameOrigin(cred, hop));  // other scheme
  CHECK(http_origin::isDowngrade(cred, hop));
  // userinfo is not part of the origin: a@evil.com style tricks resolve to the real host
  REQUIRE(http_origin::parse("https://opds.example.org@evil.example.com/x", hop));
  CHECK(hop.host == "evil.example.com");
  CHECK_FALSE(http_origin::sameOrigin(cred, hop));
}
