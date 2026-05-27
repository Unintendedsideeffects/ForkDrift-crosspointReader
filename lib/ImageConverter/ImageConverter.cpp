#include "ImageConverter.h"

#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace {
// Portable case-insensitive extension comparison
bool extEquals(const char* ext, const char* target) {
  std::string extLower(ext);
  std::transform(extLower.begin(), extLower.end(), extLower.begin(), [](unsigned char c) { return std::tolower(c); });
  return extLower == target;
}
}  // namespace

ImageConverter::Format ImageConverter::detectFormat(const char* filepath) {
  const char* ext = strrchr(filepath, '.');
  if (!ext) return FORMAT_UNKNOWN;

  // Skip the dot
  ext++;

  if (extEquals(ext, "jpg") || extEquals(ext, "jpeg")) {
    return FORMAT_JPEG;
  }

  if (extEquals(ext, "png")) {
    return FORMAT_PNG;
  }

  return FORMAT_UNKNOWN;
}

bool ImageConverter::convertToBmpStream(HalFile& imageFile, Format format, Print& bmpOut, int targetWidth,
                                        int targetHeight, bool crop) {
  switch (format) {
    case FORMAT_JPEG:
      return JpegToBmpConverter::jpegFileToBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight, crop);

    case FORMAT_PNG:
      return PngToBmpConverter::pngFileToBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight, crop);

    default:
      return false;
  }
}

bool ImageConverter::convertTo1BitBmpStream(HalFile& imageFile, Format format, Print& bmpOut, int targetWidth,
                                            int targetHeight, bool crop) {
  switch (format) {
    case FORMAT_JPEG:
      return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight, crop);

    case FORMAT_PNG:
      return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight, crop);

    default:
      return false;
  }
}
