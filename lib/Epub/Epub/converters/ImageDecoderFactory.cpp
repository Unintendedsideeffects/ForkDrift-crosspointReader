#include "ImageDecoderFactory.h"

#include <HalStorage.h>
#include <Logging.h>
#include <SpiBusMutex.h>

#include <cstdint>
#include <memory>
#include <new>
#include <string>

#if ENABLE_EPUB_GIF
#include "GifToFramebufferConverter.h"
#endif
#include "ImageMagic.h"
#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;
#if ENABLE_EPUB_GIF
std::unique_ptr<GifToFramebufferConverter> ImageDecoderFactory::gifDecoder = nullptr;
#endif

namespace {

SniffedFormat sniffImageFormat(const std::string& imagePath) {
  SpiBusMutex::Guard guard;
  HalFile file;
  if (!Storage.openFileForRead("DEC", imagePath, file)) {
    return SniffedFormat::Unknown;
  }
  uint8_t magic[8] = {0};
  const int read = file.read(magic, sizeof(magic));
  file.close();
  return sniffImageMagic(magic, read);
}
}  // namespace

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath) {
  std::string ext = imagePath;
  size_t dotPos = ext.rfind('.');
  if (dotPos != std::string::npos) {
    ext = ext.substr(dotPos);
    for (auto& c : ext) {
      c = tolower(c);
    }
  } else {
    ext = "";
  }

  bool wantJpeg = JpegToFramebufferConverter::supportsFormat(ext);
  bool wantPng = PngToFramebufferConverter::supportsFormat(ext);
#if ENABLE_EPUB_GIF
  bool wantGif = GifToFramebufferConverter::supportsFormat(ext);
#else
  bool wantGif = false;
#endif
  if (!wantJpeg && !wantPng && !wantGif) {
    switch (sniffImageFormat(imagePath)) {
      case SniffedFormat::Jpeg:
        wantJpeg = true;
        break;
      case SniffedFormat::Png:
        wantPng = true;
        break;
#if ENABLE_EPUB_GIF
      case SniffedFormat::Gif:
        wantGif = true;
        break;
#endif
      case SniffedFormat::Unknown:
        break;
    }
  }

  if (wantJpeg) {
    if (!jpegDecoder) {
      jpegDecoder.reset(new (std::nothrow) JpegToFramebufferConverter());
      if (!jpegDecoder) {
        LOG_ERR("DEC", "OOM: JpegToFramebufferConverter");
        return nullptr;
      }
    }
    return jpegDecoder.get();
  }
  if (wantPng) {
    if (!pngDecoder) {
      pngDecoder.reset(new (std::nothrow) PngToFramebufferConverter());
      if (!pngDecoder) {
        LOG_ERR("DEC", "OOM: PngToFramebufferConverter");
        return nullptr;
      }
    }
    return pngDecoder.get();
  }
#if ENABLE_EPUB_GIF
  if (wantGif) {
    if (!gifDecoder) {
      gifDecoder.reset(new (std::nothrow) GifToFramebufferConverter());
      if (!gifDecoder) {
        LOG_ERR("DEC", "OOM: GifToFramebufferConverter");
        return nullptr;
      }
    }
    return gifDecoder.get();
  }
#endif

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
