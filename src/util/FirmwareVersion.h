#pragma once

#include <cstdint>
#include <string>

// Unified on-device firmware versioning for CrossPoint.
//
// All build environments emit ONE comparable format:
//
//   MAJOR.MINOR.PATCH[-CHANNEL][+BUILD][-PROFILE]
//
//   MAJOR.MINOR.PATCH  required numeric core, sourced from [crosspoint] version
//                      in platformio.ini (e.g. "1.4.1").
//   CHANNEL            "dev" | "nightly" | "rc", or absent for stable releases.
//   BUILD              monotonic integer distinguishing rolling builds within a
//                      channel: git commit count for dev/rc, YYYYMMDD date for
//                      nightly. Absent for stable.
//   PROFILE            informational variant: lean|standard|full|slim|custom.
//                      NEVER affects ordering.
//
// Examples:
//   1.4.1                          stable release
//   1.4.1-rc+4231                  release candidate (commit 4231)
//   1.4.1-dev+8542                 rolling dev build (commit 8542)
//   1.4.1-dev+8542-full            rolling dev build, full feature profile
//   1.4.1-nightly+20260227         nightly build
//
// Ordering (newest first): higher core wins; then channel rank
// stable > rc > dev == nightly; then numeric BUILD within the same channel.
// PROFILE never participates in ordering, so upstream/downstream variants of
// the same build are interchangeable. Unknown or malformed version strings are
// never assumed newer than a known value (conservative for OTA safety).

namespace firmware_version {

// Parsed, comparable representation of a canonical version string.
//
// bool valid reflects whether the string was a recognised canonical version.
// When valid is false the other fields are unspecified; callers MUST treat an
// invalid operand as "not newer" and never as newer.
struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;

  // 0 = stable, 1 = rc, 2 = dev, 3 = nightly. Higher ordinal is NOT "newer" —
  // use compare() for ordering.
  int channel = 0;

  // Monotonic build id within a channel (commit count / date). Zero when absent.
  uint64_t build = 0;

  bool valid = false;
};

// Parse a canonical version string. Returns an invalid Version for anything
// that does not match the grammar above.
Version parse(const std::string& text);

// Compare two versions.
//   > 0  if a is newer than b
//   < 0  if a is older than b
//   == 0 if equal (same core/channel/build; profile ignored)
// Invalid operands compare as older than valid ones and equal to each other.
int compare(const Version& a, const Version& b);

}  // namespace firmware_version
