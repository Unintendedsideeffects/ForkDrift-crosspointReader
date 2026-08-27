#include "util/FirmwareVersion.h"

#include <cstring>

namespace firmware_version {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool parseUint(const char*& s, int& out) {
  if (!isDigit(*s)) return false;
  int value = 0;
  while (isDigit(*s)) {
    value = value * 10 + (*s - '0');
    if (value > 10000000) return false;  // implausible, guard overflow
    ++s;
  }
  out = value;
  return true;
}

bool parseBuildId(const char*& s, uint64_t& out) {
  // BUILD is either an unsigned integer (dev/rc commit count) or an 8-digit
  // YYYYMMDD date (nightly). Both order numerically within their channel, so we
  // only need to accumulate the digits.
  uint64_t value = 0;
  size_t ndigits = 0;
  while (isDigit(*s) && ndigits < 20) {
    value = value * 10ull + static_cast<uint64_t>(*s - '0');
    ++s;
    ++ndigits;
  }
  if (ndigits == 0) return false;
  out = value;
  return true;
}

int channelRank(int channel) {
  switch (channel) {
    case 0: return 3;  // stable
    case 1: return 2;  // rc
    case 2: return 1;  // dev
    case 3: return 1;  // nightly
    default: return 0;
  }
}

}  // namespace

Version parse(const std::string& text) {
  Version v;
  const char* s = text.c_str();

  if (!parseUint(s, v.major) || *s != '.') return v;
  ++s;
  if (!parseUint(s, v.minor) || *s != '.') return v;
  ++s;
  if (!parseUint(s, v.patch)) return v;

  v.valid = true;

  if (*s == '\0') return v;  // bare stable, e.g. "1.4.1"

  // Optional channel: "-dev", "-nightly", "-rc". Reject bare "-PROFILE" after
  // the core (we require a recognised channel before any suffix).
  if (*s == '-') {
    ++s;
    if (strncmp(s, "dev", 3) == 0) {
      v.channel = 2;
      s += 3;
    } else if (strncmp(s, "nightly", 7) == 0) {
      v.channel = 3;
      s += 7;
    } else if (strncmp(s, "rc", 2) == 0) {
      v.channel = 1;
      s += 2;
    } else {
      v.valid = false;  // not a recognised channel -> malformed
      return v;
    }
  }

  // Optional build id: "+<digits>".
  if (*s == '+') {
    ++s;
    uint64_t build = 0;
    if (!parseBuildId(s, build)) {
      v.valid = false;
      return v;
    }
    v.build = build;
  }

  // Optional trailing profile: "-lean|-standard|-full|-full_overrides|...".
  // Informational, dropped from ordering. Must be a short [a-z0-9_]+ token.
  if (*s == '-') {
    ++s;
    size_t n = 0;
    while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= '0' && s[n] <= '9') || s[n] == '_') ++n;
    if (n == 0 || n > 32 || s[n] != '\0') {
      v.valid = false;
      return v;
    }
    s += n;
  }

  if (*s != '\0') {
    v.valid = false;
    return v;
  }

  return v;
}

int compare(const Version& a, const Version& b) {
  // Invalid always sorts before valid.
  if (!a.valid && !b.valid) return 0;
  if (!a.valid) return -1;
  if (!b.valid) return 1;

  if (a.major != b.major) return a.major > b.major ? 1 : -1;
  if (a.minor != b.minor) return a.minor > b.minor ? 1 : -1;
  if (a.patch != b.patch) return a.patch > b.patch ? 1 : -1;

  const int rankA = channelRank(a.channel);
  const int rankB = channelRank(b.channel);
  if (rankA != rankB) return rankA > rankB ? 1 : -1;

  // Same channel rank: stable (rank 3) has no build id -> equal. dev/nightly
  // (rank 1) and rc (rank 2) order by build id. We do not compare build ids
  // across dev vs nightly (they use different build id units), so treat a
  // non-matching channel as incomparable-but-equal rank -> equal.
  if (a.channel != b.channel) return 0;

  if (a.build != b.build) return a.build > b.build ? 1 : -1;
  return 0;
}

}  // namespace firmware_version
