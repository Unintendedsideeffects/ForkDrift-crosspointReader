#pragma once

#include <cstdint>

// ============================================================================
// Pure PNG scanline arithmetic: sub-byte sample unpacking and vertical
// scale row-mapping, extracted so the PNG spec math can be proven in a host
// test independent of PNGdec (an ESP32-only Arduino library not present on
// the host toolchain) and the GfxRenderer/HalStorage decode path that uses
// it. PngToFramebufferConverter.cpp is the only caller.
// ============================================================================
namespace pngrow {

// PNG (ISO/IEC 15948) IHDR colour-type values. Duplicated here rather than
// including PNGdec.h so this header stays host-testable; PNGdec's own
// PNG_PIXEL_* enum is defined to the same numbers.
constexpr int kColorGrayscale = 0;
constexpr int kColorTruecolor = 2;
constexpr int kColorIndexed = 3;
constexpr int kColorGrayAlpha = 4;
constexpr int kColorTruecolorAlpha = 6;

// PNGdec's own PNGParseInfo rejects bpp > 8 before the draw callback ever
// runs (png.inl: "16-bit pixels are not supported (yet)"), so the only
// depths that reach convertLineToGray are 1, 2, 4, or 8. 1/2/4-bit samples
// are only valid for grayscale and indexed colour types per the PNG spec.
constexpr bool isSupportedBitDepth(int pixelType, int bitsPerSample) {
  if (bitsPerSample == 8) return true;
  if (bitsPerSample != 1 && bitsPerSample != 2 && bitsPerSample != 4) return false;
  return pixelType == kColorGrayscale || pixelType == kColorIndexed;
}

// Bytes needed for `width` samples of `bitsPerSample` bits each, packed
// MSB-first (PNG spec 7.2 "Scanlines"): ceil(width * bits / 8).
constexpr int packedRowBytes(int width, int bitsPerSample) { return (width * bitsPerSample + 7) / 8; }

// Read sample `x` from a scanline packed at `bitsPerSample` bits/sample,
// MSB-first: byte 0 holds samples 0..(8/bitsPerSample - 1) with sample 0 in
// the most significant bits.
inline uint8_t readPackedSample(const uint8_t* row, int x, int bitsPerSample) {
  if (bitsPerSample == 8) return row[x];
  const int bitOffset = x * bitsPerSample;
  const int shift = 8 - bitsPerSample - (bitOffset & 7);
  const uint8_t mask = static_cast<uint8_t>((1U << bitsPerSample) - 1);
  return static_cast<uint8_t>((row[bitOffset >> 3] >> shift) & mask);
}

// Scale a sub-byte grayscale sample to the full 0-255 range (PNG spec 10.2:
// full_value = sample * 255 / maxSample). Indexed samples are palette
// indices, not gray levels, and must not go through this function.
inline uint8_t expandGraySampleToByte(uint8_t sample, int bitsPerSample) {
  if (bitsPerSample == 8) return sample;
  const uint8_t maxSample = static_cast<uint8_t>((1U << bitsPerSample) - 1);
  return static_cast<uint8_t>((static_cast<unsigned>(sample) * 255U) / maxSample);
}

// Map source scanline `srcY` of `srcHeight` onto the half-open output row
// range [firstDstY, endDstY) it must paint into a `dstHeight`-row output.
//
// firstDstY = floor(srcY * dstHeight / srcHeight) is the standard nearest-
// source-row mapping. During upscaling (dstHeight > srcHeight) a single
// source row must cover every output row up to the one the NEXT source row
// claims, so endDstY = floor((srcY+1) * dstHeight / srcHeight); painting only
// firstDstY there would leave the rest of that span unpainted. `lastPaintedDstY`
// (-1 before the first call) clips a downscaled row that maps to an
// already-painted output row down to an empty range, rather than repainting it.
inline void computeDstRowRange(int srcY, int srcHeight, int dstHeight, int lastPaintedDstY, int* firstDstY,
                                int* endDstY) {
  if (srcHeight <= 0) {
    *firstDstY = 0;
    *endDstY = 0;
    return;
  }
  int first = (srcY * dstHeight) / srcHeight;
  int end = first + 1;
  if (dstHeight > srcHeight) {
    end = ((srcY + 1) * dstHeight) / srcHeight;
  }
  if (first <= lastPaintedDstY) first = lastPaintedDstY + 1;
  if (end > dstHeight) end = dstHeight;
  if (first < 0) first = 0;
  if (first > end) first = end;
  *firstDstY = first;
  *endDstY = end;
}

}  // namespace pngrow
