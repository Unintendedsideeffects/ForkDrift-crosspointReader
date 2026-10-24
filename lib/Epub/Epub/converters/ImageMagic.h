#pragma once

#include <FeatureFlags.h>

#include <cstdint>

enum class SniffedFormat {
  Unknown,
  Jpeg,
  Png,
#if ENABLE_EPUB_GIF
  Gif,
#endif
};

inline SniffedFormat sniffImageMagic(const uint8_t* magic, int n) {
  if (n >= 3 && magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
    return SniffedFormat::Jpeg;
  }
  if (n >= 8 && magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47 && magic[4] == 0x0D &&
      magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A) {
    return SniffedFormat::Png;
  }
#if ENABLE_EPUB_GIF
  if (n >= 6 && magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8' &&
      (magic[4] == '7' || magic[4] == '9') && magic[5] == 'a') {
    return SniffedFormat::Gif;
  }
#endif
  return SniffedFormat::Unknown;
}
