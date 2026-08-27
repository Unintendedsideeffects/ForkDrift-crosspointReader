#include "doctest/doctest.h"
#include "src/util/FirmwareVersion.h"

using firmware_version::parse;
using firmware_version::compare;

TEST_CASE("parse: canonical forms are recognised") {
  auto stable = parse("1.4.1");
  CHECK(stable.valid);
  CHECK(stable.major == 1);
  CHECK(stable.minor == 4);
  CHECK(stable.patch == 1);
  CHECK(stable.channel == 0);
  CHECK(stable.build == 0);

  auto rc = parse("1.4.1-rc+4231");
  CHECK(rc.valid);
  CHECK(rc.channel == 1);
  CHECK(rc.build == 4231);

  auto dev = parse("1.4.1-dev+8542");
  CHECK(dev.valid);
  CHECK(dev.channel == 2);
  CHECK(dev.build == 8542);

  auto nightly = parse("1.4.1-nightly+20260227");
  CHECK(nightly.valid);
  CHECK(nightly.channel == 3);
  CHECK(nightly.build == 20260227);
}

TEST_CASE("parse: profile suffix is accepted and ignored for ordering") {
  auto withProfile = parse("1.4.1-dev+8542-full");
  CHECK(withProfile.valid);
  CHECK(withProfile.channel == 2);
  CHECK(withProfile.build == 8542);

  CHECK(parse("1.4.1-dev+8542-lean").valid);
  CHECK(parse("1.4.1-dev+8542-standard").valid);
  CHECK(parse("1.4.1-dev+8542-slim").valid);
  CHECK(parse("1.4.1-dev+8542-full_overrides").valid);
  CHECK(parse("1.4.1-dev+8542-custom").valid);
}

TEST_CASE("parse: malformed strings are invalid") {
  CHECK_FALSE(parse("").valid);
  CHECK_FALSE(parse("1.4").valid);
  CHECK_FALSE(parse("1.4.1.2").valid);
  CHECK_FALSE(parse("1.4.x").valid);
  CHECK_FALSE(parse("abc").valid);
  // Profile without a recognised channel is not canonical.
  CHECK_FALSE(parse("1.4.1-full").valid);
  CHECK_FALSE(parse("1.4.1-slim").valid);
  // Build id must be numeric.
  CHECK_FALSE(parse("1.4.1-dev+abc").valid);
  // Trailing junk.
  CHECK_FALSE(parse("1.4.1-dev+8542-full-extra").valid);
  CHECK_FALSE(parse("1.4.1-dev+8542 ").valid);
}

TEST_CASE("compare: core version dominates") {
  CHECK(compare(parse("1.5.0"), parse("1.4.9")) > 0);
  CHECK(compare(parse("1.4.10"), parse("1.4.9")) > 0);
  CHECK(compare(parse("1.4.1"), parse("2.0.0")) < 0);
  CHECK(compare(parse("1.4.1"), parse("1.4.1")) == 0);
}

TEST_CASE("compare: stable is newer than pre-release channels at same core") {
  CHECK(compare(parse("1.4.1"), parse("1.4.1-rc+9999")) > 0);
  CHECK(compare(parse("1.4.1"), parse("1.4.1-dev+99999")) > 0);
  CHECK(compare(parse("1.4.1"), parse("1.4.1-nightly+29991231")) > 0);
  CHECK(compare(parse("1.4.1-rc+1"), parse("1.4.1-dev+99999")) > 0);
}

TEST_CASE("compare: build id orders within the same channel") {
  CHECK(compare(parse("1.4.1-dev+8543"), parse("1.4.1-dev+8542")) > 0);
  CHECK(compare(parse("1.4.1-dev+8542"), parse("1.4.1-dev+8543")) < 0);
  CHECK(compare(parse("1.4.1-rc+4231"), parse("1.4.1-rc+4229")) > 0);
  CHECK(compare(parse("1.4.1-nightly+20260228"), parse("1.4.1-nightly+20260227")) > 0);
}

TEST_CASE("compare: profile never affects ordering") {
  CHECK(compare(parse("1.4.1-dev+8542-full"), parse("1.4.1-dev+8542-lean")) == 0);
  CHECK(compare(parse("1.4.1-dev+8543-full"), parse("1.4.1-dev+8542-lean")) > 0);
  CHECK(compare(parse("1.4.1-dev+8542-full"), parse("1.4.1-dev+8543-lean")) < 0);
}

TEST_CASE("compare: dev vs nightly at equal rank is incomparable (not newer)") {
  // Different channel at the same rank — build ids use different units, so we
  // never claim one is newer. OTA must treat this conservatively.
  CHECK(compare(parse("1.4.1-dev+8542"), parse("1.4.1-nightly+20260227")) == 0);
  CHECK(compare(parse("1.4.1-nightly+20260227"), parse("1.4.1-dev+8542")) == 0);
}

TEST_CASE("compare: invalid operands are never newer") {
  CHECK(compare(parse(""), parse("1.4.1-dev+8542")) < 0);
  CHECK(compare(parse("1.4.1-dev+8542"), parse("")) > 0);
  CHECK(compare(parse(""), parse("")) == 0);
}
