#pragma once

#include <FeatureFlags.h>

#if ENABLE_EPUB_GIF

#include <cstddef>
#include <cstdint>

/*
 * First-frame GIF raster for the EPUB image path.
 *
 * Layout still uses ImageDimsProbe (LSD, no heap). This decoder then streams
 * LZW into one source row at a time: 4096-code dictionary split across heap
 * blocks (prefix 8 KB + suffix 4 KB + stack 4 KB), one 256-colour palette,
 * one 255-byte sub-block, one width-capped line. Never a W×H canvas.
 *
 * First image descriptor only. Interlace is emitted in pass order (no canvas
 * deinterlace). Transparent index is white. Truncated/oversized/zero frames
 * fail closed. Peak heap is kPeakHeapBytes, allocated only for the call.
 */
namespace gifraster {

constexpr int kLzwCodeCount = 4096;
constexpr int kMaxLineWidth = 2048;
constexpr int kMaxSourcePixels = 3145728;
constexpr size_t kPrefixBytes = static_cast<size_t>(kLzwCodeCount) * sizeof(uint16_t);
constexpr size_t kSuffixBytes = static_cast<size_t>(kLzwCodeCount);
constexpr size_t kStackBytes = static_cast<size_t>(kLzwCodeCount);
constexpr size_t kPaletteBytes = 256 * 3;
constexpr size_t kSubBlockBytes = 256;
constexpr size_t kPeakHeapBytes =
    kPrefixBytes + kSuffixBytes + kStackBytes + kPaletteBytes + kSubBlockBytes + static_cast<size_t>(kMaxLineWidth);

enum class Status : uint8_t {
  Ok = 0,
  Truncated,
  BadHeader,
  TooLarge,
  Oom,
  DecodeError,
};

struct DecodeInfo {
  int width = 0;
  int height = 0;
  bool interlaced = false;
};

struct ByteSource {
  int (*read)(void* ctx, uint8_t* dst, int n);
  void* ctx;
};

using BeginFn = bool (*)(void* ctx, const DecodeInfo& info);
using RowFn = bool (*)(void* ctx, int y, const uint8_t* gray, int width);

Status decode(ByteSource src, BeginFn onBegin, RowFn onRow, void* ctx);
Status decodeMemory(const uint8_t* data, size_t size, BeginFn onBegin, RowFn onRow, void* ctx);

inline const char* statusText(Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::Truncated:
      return "truncated";
    case Status::BadHeader:
      return "bad header";
    case Status::TooLarge:
      return "too large";
    case Status::Oom:
      return "oom";
    case Status::DecodeError:
      return "decode error";
  }
  return "unknown";
}

}  // namespace gifraster

#endif
