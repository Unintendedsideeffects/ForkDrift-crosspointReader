#include "ImageToFramebufferDecoder.h"

#include <Logging.h>

bool ImageToFramebufferDecoder::validateImageDimensions(int width, int height, const std::string& format) {
  // Reject non-positive dims: a decoder that fails to open returns 0x0, which would
  // otherwise pass the cap (0 < MAX) and drive a 0-width Bresenham loop into a hang.
  // Negative values arise from int16_t truncation of an oversized dimension.
  // We also reject width/height exceeding INT16_MAX to prevent truncation wrap-around.
  if (width <= 0 || height <= 0 || width > INT16_MAX || height > INT16_MAX) {
    LOG_ERR("IMG", "Invalid image dimensions (%dx%d %s)", width, height, format.c_str());
    return false;
  }
  // 64-bit product: width*height as int overflows for crafted dims (e.g. 46341*46341
  // wraps negative and slips under the cap), bypassing this guard on a malicious image.
  if (static_cast<int64_t>(width) * static_cast<int64_t>(height) > MAX_SOURCE_PIXELS) {
    LOG_ERR("IMG", "Image too large (%dx%d %s), max supported: %d pixels", width, height, format.c_str(),
            MAX_SOURCE_PIXELS);
    return false;
  }
  return true;
}

void ImageToFramebufferDecoder::warnUnsupportedFeature(const std::string& feature, const std::string& imagePath) {
  LOG_WRN("IMG", "Unsupported feature '%s' in image '%s'. Image may not display correctly.", feature.c_str(),
          imagePath.c_str());
}