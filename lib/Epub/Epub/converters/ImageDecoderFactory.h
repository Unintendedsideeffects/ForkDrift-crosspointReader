#pragma once
#include <FeatureFlags.h>

#include <cstdint>
#include <memory>
#include <string>

#include "ImageToFramebufferDecoder.h"

class JpegToFramebufferConverter;
class PngToFramebufferConverter;
#if ENABLE_EPUB_GIF
class GifToFramebufferConverter;
#endif

class ImageDecoderFactory {
 public:
  // Returns non-owning pointer - factory owns the decoder lifetime
  static ImageToFramebufferDecoder* getDecoder(const std::string& imagePath);
  static bool isFormatSupported(const std::string& imagePath);

 private:
  static std::unique_ptr<JpegToFramebufferConverter> jpegDecoder;
  static std::unique_ptr<PngToFramebufferConverter> pngDecoder;
#if ENABLE_EPUB_GIF
  static std::unique_ptr<GifToFramebufferConverter> gifDecoder;
#endif
};
