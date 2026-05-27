#pragma once

#include <cctype>
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
//   firmware-([a-z]+-)?(\d{8})(-\d{4})?-([0-9a-fA-F]{7,})(-dirty)?\.bin
//     [a-z]+-    OPTIONAL build profile (lean|standard|full|custom|slim|…)
//     \d{8}      build date (YYYYMMDD)
//     -\d{4}     OPTIONAL local-build time (HHMM); CI omits it
//     [hex]{7,}  git short sha (>= 7 chars)
//     -dirty     OPTIONAL marker: built from a tree with uncommitted source
//
// Profile vs date disambiguation: profile tokens are all-lowercase-alpha; dates
// always start with a decimal digit — so the first character after "firmware-"
// is unambiguous.  The profile segment is optional so pre-profile binaries
// remain recognizable after a firmware update.
//
// Accepted examples:
//   firmware-20260518-8ec4ffe.bin                      (CI / release, no profile)
//   firmware-full-20260518-8ec4ffe.bin                 (CI / release, full profile)
//   firmware-20260518-1420-8ec4ffe.bin                 (local, clean, no profile)
//   firmware-lean-20260518-1420-8ec4ffe.bin            (local, clean, lean profile)
//   firmware-standard-20260518-1420-8ec4ffe-dirty.bin  (local, uncommitted)
//   firmware-20260518-1234567.bin                      (all-decimal sha, no time)
//
// Host test that pins this contract: test/host/test_firmware_artifact_name.cpp
// ─────────────────────────────────────────────────────────────────────────────

namespace firmware_artifact {

inline bool isDecimalDigit(const char ch) { return ch >= '0' && ch <= '9'; }

inline bool isHexDigit(const char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

inline bool isLowerAlpha(const char ch) { return ch >= 'a' && ch <= 'z'; }

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
  // Shortest legal name: prefix + date + '-' + 7-hex + suffix (no profile, no time).
  const size_t minLength = prefixLength + dateLength + 1 + minShaLength + suffixLength;

  if (nameLength < minLength || strncmp(name, prefix, prefixLength) != 0 ||
      strcmp(name + nameLength - suffixLength, suffix) != 0) {
    return false;
  }

  size_t pos = prefixLength;

  // Optional profile token: one or more lowercase-alpha chars followed by '-'.
  // Disambiguated from the date by the first character: alpha → profile, digit → date.
  if (pos < nameLength && isLowerAlpha(name[pos])) {
    const size_t profileStart = pos;
    while (pos < nameLength && name[pos] != '-') {
      if (!isLowerAlpha(name[pos])) {
        return false;  // profile token must be all lowercase alpha
      }
      ++pos;
    }
    if (pos <= profileStart || pos >= nameLength || name[pos] != '-') {
      return false;  // empty token or no trailing '-'
    }
    ++pos;  // skip past the '-' separator
  }

  // <date> : exactly dateLength decimal digits, followed by '-'.
  const size_t dateStart = pos;
  if (nameLength < pos + dateLength + 1 || name[dateStart + dateLength] != '-') {
    return false;
  }
  for (size_t i = dateStart; i < dateStart + dateLength; ++i) {
    if (!isDecimalDigit(name[i])) {
      return false;
    }
  }

  pos = dateStart + dateLength + 1;  // first char after "<date>-"
  size_t shaEnd = nameLength - suffixLength;

  // Optional trailing "-dirty" immediately before ".bin".
  if (shaEnd >= pos + dirtyTagLength && strncmp(name + shaEnd - dirtyTagLength, dirtyTag, dirtyTagLength) == 0) {
    shaEnd -= dirtyTagLength;
  }

  // Optional "HHMM-" time segment between the date and the sha. A sha never
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
