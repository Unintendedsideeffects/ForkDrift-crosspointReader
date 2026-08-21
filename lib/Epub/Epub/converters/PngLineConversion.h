#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "PngRowExpansion.h"

// ============================================================================
// PNGdec scanline -> grayscale-line conversion, and the internal-buffer-size
// preflight, factored out of PngToFramebufferConverter.cpp's anonymous
// namespace so a host test can call them directly with a synthetic source
// buffer. Neither of these takes a PNGdec type (PNGDRAW/PNGFILE) or touches
// GfxRenderer -- both take only the plain ints/pointers that PNGdec's draw
// callback would otherwise unpack for them, which is exactly the "wiring" a
// swapped argument or a pitch/width mix-up would break.
//
// See PngToFramebufferConverter.cpp's pngDrawCallback for the one production
// call site (real PNGdec-supplied values) and the comment there for exactly
// what remains untestable on host and why.
// ============================================================================
namespace pngrow {

// Bytes per pixel for 8-bit-or-wider PNG pixel types. Sub-8-bit grayscale/
// indexed samples are packed (see packedRowBytes above) so this only applies
// at bitsPerSample == 8. Values per PNG spec 6.2 IHDR colour type.
inline int bytesPerPixelFromType(int pixelType) {
  switch (pixelType) {
    case kColorTruecolor:
      return 3;
    case kColorGrayAlpha:
      return 2;
    case kColorTruecolorAlpha:
      return 4;
    case kColorGrayscale:
    case kColorIndexed:
    default:
      return 1;
  }
}

// PNGdec keeps TWO scanlines in its internal ucPixels buffer (current +
// previous), each with a leading filter byte: required storage is
// approximately 2 * (pitch + 1) + alignment slack. If PNG_MAX_BUFFERED_PIXELS
// is smaller than this for a given image, PNGdec can overrun its internal
// buffer before the draw callback ever runs. Returns SIZE_MAX for a width
// PNGdec could never have produced (<=0) or a pitch large enough to risk
// overflowing the size_t multiply, so callers reject rather than
// under-allocate.
inline size_t requiredPngInternalBufferBytes(int srcWidth, int pixelType, int bitsPerSample) {
  if (srcWidth <= 0) {
    return SIZE_MAX;
  }
  int64_t pitch;
  if ((pixelType == kColorGrayscale || pixelType == kColorIndexed) && bitsPerSample < 8) {
    pitch = packedRowBytes(srcWidth, bitsPerSample);
  } else {
    pitch = static_cast<int64_t>(srcWidth) * bytesPerPixelFromType(pixelType);
  }
  constexpr int64_t kMaxPitch = 1000000;  // Cap to prevent overflow
  if (pitch > kMaxPitch) {
    return SIZE_MAX;
  }
  const int64_t total = ((pitch + 1) * 2) + 32;
  return static_cast<size_t>(total);
}

// Convert one decoded PNGdec scanline to grayscale, alpha-blended onto a
// white background. Low-bit-depth grayscale/indexed rows are packed MSB-first
// (PNG spec 7.2) and are expanded sample-by-sample via readPackedSample/
// expandGraySampleToByte; indexed samples are palette indices and must not go
// through expandGraySampleToByte directly. An indexed row with no palette
// (spec-violating: indexed colour type without a PLTE chunk) falls back to
// treating the raw index as a gray level -- a pre-existing extension of the
// 8-bit behaviour, not new risk (see docs/FINDINGS.md).
inline void convertLineToGray(const uint8_t* pPixels, uint8_t* grayLine, int width, int pixelType, int bitsPerSample,
                              const uint8_t* palette, int hasAlpha) {
  switch (pixelType) {
    case kColorGrayscale:
      if (bitsPerSample == 8) {
        memcpy(grayLine, pPixels, static_cast<size_t>(width));
      } else {
        for (int x = 0; x < width; x++) {
          grayLine[x] = expandGraySampleToByte(readPackedSample(pPixels, x, bitsPerSample), bitsPerSample);
        }
      }
      break;

    case kColorTruecolor:
      for (int x = 0; x < width; x++) {
        const uint8_t* p = &pPixels[x * 3];
        grayLine[x] = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
      }
      break;

    case kColorIndexed:
      if (palette) {
        if (hasAlpha) {
          for (int x = 0; x < width; x++) {
            const uint8_t idx = readPackedSample(pPixels, x, bitsPerSample);
            const uint8_t* p = &palette[idx * 3];
            const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            const uint8_t alpha = palette[768 + idx];
            grayLine[x] = static_cast<uint8_t>((gray * alpha + 255 * (255 - alpha)) / 255);
          }
        } else {
          for (int x = 0; x < width; x++) {
            const uint8_t idx = readPackedSample(pPixels, x, bitsPerSample);
            const uint8_t* p = &palette[idx * 3];
            grayLine[x] = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          }
        }
      } else if (bitsPerSample == 8) {
        memcpy(grayLine, pPixels, static_cast<size_t>(width));
      } else {
        for (int x = 0; x < width; x++) {
          grayLine[x] = expandGraySampleToByte(readPackedSample(pPixels, x, bitsPerSample), bitsPerSample);
        }
      }
      break;

    case kColorGrayAlpha:
      for (int x = 0; x < width; x++) {
        const uint8_t gray = pPixels[x * 2];
        const uint8_t alpha = pPixels[x * 2 + 1];
        grayLine[x] = static_cast<uint8_t>((gray * alpha + 255 * (255 - alpha)) / 255);
      }
      break;

    case kColorTruecolorAlpha:
      for (int x = 0; x < width; x++) {
        const uint8_t* p = &pPixels[x * 4];
        const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        const uint8_t alpha = p[3];
        grayLine[x] = static_cast<uint8_t>((gray * alpha + 255 * (255 - alpha)) / 255);
      }
      break;

    default:
      memset(grayLine, 128, static_cast<size_t>(width));
      break;
  }
}

// Destination row range plus expanded grayscale line for one PNGdec scanline
// callback. This is the entirety of pngDrawCallback's "wiring": which
// ctx/pDraw field feeds which parameter (srcWidth vs a pitch, bitsPerSample
// vs a pixel type, ...), and how the row range from computeDstRowRange gates
// the conversion. Taking plain ints/pointers instead of a PngContext*/
// PNGDRAW* is what makes this callable from a host test with hand-built
// stand-in values for both.
struct LineWriteRange {
  int firstDstY;
  int endDstY;
};

inline LineWriteRange prepareGrayLine(int srcY, int srcWidth, int srcHeight, int dstHeight, int lastPaintedDstY,
                                      const uint8_t* pPixels, int pixelType, int bitsPerSample, const uint8_t* palette,
                                      int hasAlpha, uint8_t* outGrayLine) {
  int firstDstY, endDstY;
  computeDstRowRange(srcY, srcHeight, dstHeight, lastPaintedDstY, &firstDstY, &endDstY);
  if (firstDstY < endDstY) {
    convertLineToGray(pPixels, outGrayLine, srcWidth, pixelType, bitsPerSample, palette, hasAlpha);
  }
  return {firstDstY, endDstY};
}

}  // namespace pngrow
