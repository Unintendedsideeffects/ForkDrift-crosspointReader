#pragma once

#include <HalStorage.h>

class Print;

class ImageConverter {
 public:
  enum Format { FORMAT_UNKNOWN, FORMAT_JPEG, FORMAT_PNG };

  // Detect format from file extension
  static Format detectFormat(const char* filepath);

  // Convert image to BMP stream with scaling
  static bool convertToBmpStream(HalFile& imageFile, Format format, Print& bmpOut, int targetWidth, int targetHeight,
                                 bool crop = true);

  // Convert image to 1-bit BMP stream (for thumbnails)
  static bool convertTo1BitBmpStream(HalFile& imageFile, Format format, Print& bmpOut, int targetWidth, int targetHeight,
                                     bool crop = true);
};
