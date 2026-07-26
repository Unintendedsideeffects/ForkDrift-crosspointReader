#include "ImageDecoderFactory.h"

#include <HalStorage.h>
#include <Logging.h>
#include <SpiBusMutex.h>

#include <cstdint>
#include <memory>
#include <string>

#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;

namespace {
enum class SniffedFormat { Unknown, Jpeg, Png };

// Detect the image format from magic bytes. Needed for EPUBs that name images
// without a file extension (e.g. O'Reilly's "media/file14"), where the
// extension-based lookup below finds nothing. Only meaningful for a real file
// on storage (the extracted cache copy), not an EPUB-internal path.
SniffedFormat sniffImageFormat(const std::string& imagePath) {
  SpiBusMutex::Guard guard;
  HalFile file;
  if (!Storage.openFileForRead("DEC", imagePath, file)) {
    return SniffedFormat::Unknown;
  }
  uint8_t magic[8] = {0};
  const int read = file.read(magic, sizeof(magic));
  file.close();
  if (read >= 3 && magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
    return SniffedFormat::Jpeg;
  }
  if (read >= 8 && magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47 && magic[4] == 0x0D &&
      magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A) {
    return SniffedFormat::Png;
  }
  return SniffedFormat::Unknown;
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
  // Missing/unknown extension: fall back to content sniffing (extensionless
  // O'Reilly-style images). No-op when imagePath is not a readable file.
  if (!wantJpeg && !wantPng) {
    switch (sniffImageFormat(imagePath)) {
      case SniffedFormat::Jpeg:
        wantJpeg = true;
        break;
      case SniffedFormat::Png:
        wantPng = true;
        break;
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
  } else if (wantPng) {
    if (!pngDecoder) {
      pngDecoder.reset(new (std::nothrow) PngToFramebufferConverter());
      if (!pngDecoder) {
        LOG_ERR("DEC", "OOM: PngToFramebufferConverter");
        return nullptr;
      }
    }
    return pngDecoder.get();
  }

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
