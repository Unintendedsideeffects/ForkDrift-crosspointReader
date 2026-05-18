#include "doctest/doctest.h"
#include "src/util/FirmwareArtifactName.h"

// Pins the *recognizer* half of the firmware artifact filename contract.
// The *producer* half is scripts/name_firmware_artifact.py. Every form that
// build_artifact_name() in that script can emit MUST be accepted here, or the
// device's boot -> SD-root -> flash path silently ignores the dropped binary.
//
// If you change the naming scheme, the failing assertions below should be
// updated in lockstep with the script AND src/util/FirmwareArtifactName.h.

using firmware_artifact::isMatchingName;

TEST_CASE("firmware artifact name: every producer-emitted form is accepted") {
  // is_ci() branch: f"firmware-{date}-{sha}.bin" (sha = GITHUB_SHA[:7], hex).
  CHECK(isMatchingName("firmware-20260518-8ec4ffe.bin"));
  // Local clean: f"firmware-{date}-{HHMM}-{sha}.bin".
  CHECK(isMatchingName("firmware-20260518-1420-8ec4ffe.bin"));
  // Local dirty: f"firmware-{date}-{HHMM}-{sha}-dirty.bin".
  CHECK(isMatchingName("firmware-20260518-1420-8ec4ffe-dirty.bin"));
  // get_short_sha() fallback returns the all-decimal "0000000" (still 7 hex).
  CHECK(isMatchingName("firmware-20260518-0000000.bin"));
  CHECK(isMatchingName("firmware-20260518-1420-0000000.bin"));
  CHECK(isMatchingName("firmware-20260518-1420-0000000-dirty.bin"));
  // git rev-parse --short=7 widens the sha when 7 chars are ambiguous.
  CHECK(isMatchingName("firmware-20260518-8ec4ffea1.bin"));
  CHECK(isMatchingName("firmware-20260518-2359-deadBEEF12.bin"));
}

TEST_CASE("firmware artifact name: regression for the pre-extension recognizer") {
  // These are exactly the names the OLD isNamedFirmwareFile() rejected because
  // it treated the whole post-date span as the sha: the internal '-' before the
  // HHMM time and before the -dirty marker failed its isHexDigit() scan. The
  // new grammar must accept them — this is the drift that motivated the split.
  CHECK(isMatchingName("firmware-20260518-1420-8ec4ffe.bin"));        // time segment
  CHECK(isMatchingName("firmware-20260518-8ec4ffe-dirty.bin"));       // dirty, CI-shaped
  CHECK(isMatchingName("firmware-20260518-1420-8ec4ffe-dirty.bin"));  // time + dirty
}

TEST_CASE("firmware artifact name: all-decimal sha is not mistaken for a time") {
  // pos+4 is not a '-', so "1234567" is the sha, not HHMM "1234" + "567".
  CHECK(isMatchingName("firmware-20260518-1234567.bin"));
  CHECK(isMatchingName("firmware-20260518-12345678.bin"));
  // Here pos+4 IS a '-', so "1420" is the time and "1234567" is the sha.
  CHECK(isMatchingName("firmware-20260518-1420-1234567.bin"));
}

TEST_CASE("firmware artifact name: malformed names are rejected") {
  CHECK_FALSE(isMatchingName(nullptr));
  CHECK_FALSE(isMatchingName(""));
  CHECK_FALSE(isMatchingName("firmware.bin"));
  CHECK_FALSE(isMatchingName("README.md"));
  // Wrong prefix / suffix.
  CHECK_FALSE(isMatchingName("Firmware-20260518-8ec4ffe.bin"));
  CHECK_FALSE(isMatchingName("fw-20260518-8ec4ffe.bin"));
  CHECK_FALSE(isMatchingName("firmware-20260518-8ec4ffe.bin.bak"));
  CHECK_FALSE(isMatchingName("firmware-20260518-8ec4ffe.elf"));
  // Date wrong length / not digits / missing separator.
  CHECK_FALSE(isMatchingName("firmware-2026051-8ec4ffe.bin"));
  CHECK_FALSE(isMatchingName("firmware-2026X518-8ec4ffe.bin"));
  CHECK_FALSE(isMatchingName("firmware-202605188ec4ffe.bin"));
  // Sha too short (< 7) and non-hex.
  CHECK_FALSE(isMatchingName("firmware-20260518-abc.bin"));
  CHECK_FALSE(isMatchingName("firmware-20260518-zzzzzzz.bin"));
  CHECK_FALSE(isMatchingName("firmware-20260518-1420-abc.bin"));
  // Trailing marker that is not exactly "-dirty".
  CHECK_FALSE(isMatchingName("firmware-20260518-8ec4ffe-stale.bin"));
  CHECK_FALSE(isMatchingName("firmware-20260518-8ec4ffe-.bin"));
  // "-dirty" present but nothing left for a valid sha.
  CHECK_FALSE(isMatchingName("firmware-20260518--dirty.bin"));
}
