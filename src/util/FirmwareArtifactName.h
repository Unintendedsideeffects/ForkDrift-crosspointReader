#pragma once

#include <cstddef>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Firmware artifact filename contract.
//
// This is the *recognizer* half of a two-ended contract. The *producer* half is
// scripts/name_firmware_artifact.py. They MUST stay in lockstep: a name the
// producer emits must be accepted here, or the boot-time SD-root local-update
// flow (FirmwareUpdateUtil::findNamedLocalUpdatePath) will silently ignore the
// dropped binary.
//
// Grammar (PCRE-ish):
//   firmware-(\d{8})(-\d{4})?-([0-9a-fA-F]{7,})(-dirty)?\.bin
//     \d{8}      build date (YYYYMMDD)
//     -\d{4}     OPTIONAL local-build time (HHMM); CI omits it
//     [hex]{7,}  git short sha (>= 7 chars)
//     -dirty     OPTIONAL marker: built from a tree with uncommitted source
//
// Accepted examples:
//   firmware-20260518-8ec4ffe.bin                 (CI / release)
//   firmware-20260518-1420-8ec4ffe.bin            (local, clean)
//   firmware-20260518-1420-8ec4ffe-dirty.bin      (local, uncommitted)
//   firmware-20260518-1234567.bin                 (all-decimal sha, no time)
//
// Host test that pins this contract: test/host/test_firmware_artifact_name.cpp
// ─────────────────────────────────────────────────────────────────────────────

namespace firmware_artifact {

inline bool isDecimalDigit(const char ch) { return ch >= '0' && ch <= '9'; }

inline bool isHexDigit(const char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

// True iff `name` (a bare filename, not a path) matches the grammar above.
inline bool isMatchingName(const char* name) {
  constexpr char prefix[] = "firmware-";
  constexpr char suffix[] = ".bin";
  constexpr char dirtyTag[] = "-dirty";
  constexpr size_t dateLength = 8;
  constexpr size_t timeLength = 4;
  constexpr size_t minShaLength = 7;

  const size_t prefixLength = strlen(prefix);
  const size_t suffixLength = strlen(suffix);
  const size_t dirtyTagLength = strlen(dirtyTag);
  const size_t nameLength = name ? strlen(name) : 0;
  // Shortest legal name is the CI form: prefix + date + '-' + 7-hex + suffix.
  const size_t minLength = prefixLength + dateLength + 1 + minShaLength + suffixLength;

  if (nameLength < minLength || strncmp(name, prefix, prefixLength) != 0 ||
      strcmp(name + nameLength - suffixLength, suffix) != 0) {
    return false;
  }

  // <date> : exactly dateLength decimal digits, followed by '-'.
  const size_t dateStart = prefixLength;
  if (name[dateStart + dateLength] != '-') {
    return false;
  }
  for (size_t i = dateStart; i < dateStart + dateLength; ++i) {
    if (!isDecimalDigit(name[i])) {
      return false;
    }
  }

  size_t pos = dateStart + dateLength + 1;  // first char after "<date>-"
  size_t shaEnd = nameLength - suffixLength;

  // Optional trailing "-dirty" immediately before ".bin".
  if (shaEnd >= pos + dirtyTagLength && strncmp(name + shaEnd - dirtyTagLength, dirtyTag, dirtyTagLength) == 0) {
    shaEnd -= dirtyTagLength;
  }

  // Optional "HHHH-" time segment between the date and the sha. A sha never
  // contains an internal '-', so the first '-' after the date can only be the
  // time separator — this disambiguates it from an all-decimal sha.
  if (shaEnd >= pos + timeLength + 1 && name[pos + timeLength] == '-') {
    bool timeIsDigits = true;
    for (size_t i = pos; i < pos + timeLength; ++i) {
      if (!isDecimalDigit(name[i])) {
        timeIsDigits = false;
        break;
      }
    }
    if (timeIsDigits) {
      pos += timeLength + 1;
    }
  }

  // Remaining [pos, shaEnd) is the git short sha: >= minShaLength hex chars.
  if (shaEnd < pos + minShaLength) {
    return false;
  }
  for (size_t i = pos; i < shaEnd; ++i) {
    if (!isHexDigit(name[i])) {
      return false;
    }
  }

  return true;
}

}  // namespace firmware_artifact
