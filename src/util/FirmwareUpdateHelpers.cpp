#include "util/FirmwareUpdateHelpers.h"

namespace firmware_update {

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

bool isFirmwareVersionChar(const char ch) { return ch >= '!' && ch <= '~' && ch != '"' && ch != '\\'; }

void advanceMarkerMatch(const char ch, const char* marker, size_t& matchLength) {
  if (ch == marker[matchLength]) {
    ++matchLength;
    return;
  }

  matchLength = ch == marker[0] ? 1 : 0;
}

std::string extractVersionAfterMarker(const uint8_t* data, size_t length, const char* marker) {
  size_t matchLength = 0;
  bool readingVersion = false;
  std::string version;
  constexpr size_t kMaxFirmwareVersionLength = 96;

  for (size_t i = 0; i < length; ++i) {
    const char ch = static_cast<char>(data[i]);
    if (readingVersion) {
      if (ch != '\0' && isFirmwareVersionChar(ch) && version.length() < kMaxFirmwareVersionLength) {
        version += ch;
        continue;
      }
      break;
    }

    advanceMarkerMatch(ch, marker, matchLength);
    if (marker[matchLength] == '\0') {
      readingVersion = true;
      version = "";
      matchLength = 0;
    }
  }

  return version;
}

}  // namespace firmware_update
