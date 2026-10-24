#include "GifRaster.h"

#if ENABLE_EPUB_GIF

#include <HeapGuard.h>
#include <Memory.h>

#include <cstdint>
#include <cstring>

namespace gifraster {
namespace {

constexpr int kInterlaceStart[4] = {0, 4, 2, 1};
constexpr int kInterlaceStep[4] = {8, 8, 4, 2};

struct Decoder {
  ByteSource src;
  BeginFn onBegin;
  RowFn onRow;
  void* cbCtx;
  uint16_t* prefix;
  uint8_t* suffix;
  uint8_t* stack;
  uint8_t* palette;
  uint8_t* block;
  uint8_t* line;
  int width;
  int height;
  int paletteCount;
  int blockLen;
  int blockPos;
  uint32_t bitBuf;
  int bitCount;
  int x;
  int y;
  int pass;
  int emitted;
  int total;
  bool interlaced;
  bool hasTrans;
  uint8_t transIndex;
};

struct MemCursor {
  const uint8_t* data;
  size_t size;
  size_t pos;
};

int pull(Decoder& d, uint8_t* dst, int n) {
  if (!d.src.read || n <= 0) {
    return 0;
  }
  const int got = d.src.read(d.src.ctx, dst, n);
  return got < 0 ? 0 : got;
}

int readU8(Decoder& d) {
  uint8_t b = 0;
  if (pull(d, &b, 1) != 1) {
    return -1;
  }
  return b;
}

bool readExact(Decoder& d, uint8_t* dst, int n) { return n <= 0 || pull(d, dst, n) == n; }

bool skipBytes(Decoder& d, int n) {
  uint8_t tmp[16];
  while (n > 0) {
    const int chunk = n < static_cast<int>(sizeof(tmp)) ? n : static_cast<int>(sizeof(tmp));
    if (!readExact(d, tmp, chunk)) {
      return false;
    }
    n -= chunk;
  }
  return true;
}

bool skipSubBlocks(Decoder& d) {
  for (;;) {
    const int n = readU8(d);
    if (n < 0) {
      return false;
    }
    if (n == 0) {
      return true;
    }
    if (!skipBytes(d, n)) {
      return false;
    }
  }
}

int readBlockByte(Decoder& d) {
  if (d.blockPos >= d.blockLen) {
    const int n = readU8(d);
    if (n <= 0) {
      return -1;
    }
    if (!readExact(d, d.block, n)) {
      return -1;
    }
    d.blockLen = n;
    d.blockPos = 0;
  }
  return d.block[d.blockPos++];
}

int readBits(Decoder& d, int width) {
  while (d.bitCount < width) {
    const int b = readBlockByte(d);
    if (b < 0) {
      return -1;
    }
    d.bitBuf |= static_cast<uint32_t>(b) << d.bitCount;
    d.bitCount += 8;
  }
  const int code = static_cast<int>(d.bitBuf) & ((1 << width) - 1);
  d.bitBuf >>= width;
  d.bitCount -= width;
  return code;
}

bool emitPixel(Decoder& d, uint8_t index) {
  if (d.emitted >= d.total) {
    return true;
  }
  uint8_t gray = 255;
  if (!(d.hasTrans && index == d.transIndex) && index < d.paletteCount) {
    const uint8_t* p = d.palette + static_cast<size_t>(index) * 3;
    gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
  }
  d.line[d.x] = gray;
  d.x++;
  if (d.x == d.width) {
    if (d.onRow && !d.onRow(d.cbCtx, d.y, d.line, d.width)) {
      return false;
    }
    d.x = 0;
    if (d.interlaced) {
      d.y += kInterlaceStep[d.pass];
      while (d.y >= d.height && d.pass < 3) {
        d.pass++;
        d.y = kInterlaceStart[d.pass];
      }
    } else {
      d.y++;
    }
  }
  d.emitted++;
  return true;
}

Status decodeLzw(Decoder& d, int minCodeSize) {
  if (minCodeSize < 2 || minCodeSize > 8) {
    return Status::DecodeError;
  }
  const int clearCode = 1 << minCodeSize;
  const int eoiCode = clearCode + 1;
  int codeSize = minCodeSize + 1;
  int nextCode = eoiCode + 1;
  int oldCode = -1;
  d.bitBuf = 0;
  d.bitCount = 0;
  d.blockLen = 0;
  d.blockPos = 0;
  d.x = 0;
  d.y = 0;
  d.pass = 0;
  d.emitted = 0;
  d.total = d.width * d.height;

  for (int i = 0; i < clearCode; i++) {
    d.suffix[i] = static_cast<uint8_t>(i);
  }

  while (d.emitted < d.total) {
    const int code = readBits(d, codeSize);
    if (code < 0) {
      return Status::Truncated;
    }
    if (code == eoiCode) {
      return Status::Truncated;
    }
    if (code == clearCode) {
      codeSize = minCodeSize + 1;
      nextCode = eoiCode + 1;
      oldCode = -1;
      continue;
    }
    if (code > nextCode) {
      return Status::DecodeError;
    }
    const bool kwkwk = code == nextCode;
    if (kwkwk && oldCode < 0) {
      return Status::DecodeError;
    }

    uint8_t first = 0;
    if (oldCode < 0) {
      if (code >= clearCode) {
        return Status::DecodeError;
      }
      if (!emitPixel(d, static_cast<uint8_t>(code))) {
        return Status::DecodeError;
      }
      oldCode = code;
      continue;
    }

    int cursor = kwkwk ? oldCode : code;
    int sp = 0;
    while (cursor >= clearCode) {
      if (sp >= kLzwCodeCount || cursor >= nextCode) {
        return Status::DecodeError;
      }
      d.stack[sp++] = d.suffix[cursor];
      cursor = d.prefix[cursor];
    }
    if (cursor < 0 || cursor >= clearCode) {
      return Status::DecodeError;
    }
    d.stack[sp++] = static_cast<uint8_t>(cursor);
    first = static_cast<uint8_t>(cursor);
    while (sp > 0) {
      if (!emitPixel(d, d.stack[--sp])) {
        return Status::DecodeError;
      }
    }
    if (kwkwk && !emitPixel(d, first)) {
      return Status::DecodeError;
    }
    if (nextCode < kLzwCodeCount) {
      d.prefix[nextCode] = static_cast<uint16_t>(oldCode);
      d.suffix[nextCode] = first;
      nextCode++;
      if (nextCode == (1 << codeSize) && codeSize < 12) {
        codeSize++;
      }
    }
    oldCode = code;
  }
  return Status::Ok;
}

bool loadColorTable(Decoder& d, int count) {
  if (count <= 0 || count > 256) {
    return false;
  }
  if (!readExact(d, d.palette, count * 3)) {
    return false;
  }
  d.paletteCount = count;
  return true;
}

Status parseAndDecode(Decoder& d) {
  uint8_t sig[6];
  if (!readExact(d, sig, 6)) {
    return Status::Truncated;
  }
  if (sig[0] != 'G' || sig[1] != 'I' || sig[2] != 'F' || sig[3] != '8' || (sig[4] != '7' && sig[4] != '9') ||
      sig[5] != 'a') {
    return Status::BadHeader;
  }

  uint8_t lsd[7];
  if (!readExact(d, lsd, 7)) {
    return Status::Truncated;
  }
  const int packedLsd = lsd[4];
  const bool hasGct = (packedLsd & 0x80) != 0;
  const int gctCount = hasGct ? (2 << (packedLsd & 7)) : 0;

  d.hasTrans = false;
  d.transIndex = 0;
  d.paletteCount = 0;

  if (hasGct && !loadColorTable(d, gctCount)) {
    return Status::Truncated;
  }

  for (;;) {
    const int intro = readU8(d);
    if (intro < 0) {
      return Status::Truncated;
    }
    if (intro == 0x3B) {
      return Status::BadHeader;
    }
    if (intro == 0x21) {
      const int label = readU8(d);
      if (label < 0) {
        return Status::Truncated;
      }
      if (label == 0xF9) {
        const int n = readU8(d);
        if (n < 0) {
          return Status::Truncated;
        }
        if (n != 4) {
          if (!skipBytes(d, n) || !skipSubBlocks(d)) {
            return Status::Truncated;
          }
          continue;
        }
        const int packed = readU8(d);
        if (packed < 0 || !skipBytes(d, 2)) {
          return Status::Truncated;
        }
        const int tidx = readU8(d);
        const int term = readU8(d);
        if (tidx < 0 || term < 0) {
          return Status::Truncated;
        }
        d.hasTrans = (packed & 0x01) != 0;
        d.transIndex = static_cast<uint8_t>(tidx);
        continue;
      }
      if (!skipSubBlocks(d)) {
        return Status::Truncated;
      }
      continue;
    }
    if (intro != 0x2C) {
      return Status::DecodeError;
    }

    uint8_t id[9];
    if (!readExact(d, id, 9)) {
      return Status::Truncated;
    }
    d.width = id[4] | (id[5] << 8);
    d.height = id[6] | (id[7] << 8);
    const int packedImg = id[8];
    d.interlaced = (packedImg & 0x40) != 0;
    const bool hasLct = (packedImg & 0x80) != 0;
    const int lctCount = hasLct ? (2 << (packedImg & 7)) : 0;

    if (d.width <= 0 || d.height <= 0) {
      return Status::TooLarge;
    }
    if (d.width > kMaxLineWidth || d.height > INT16_MAX) {
      return Status::TooLarge;
    }
    if (static_cast<int64_t>(d.width) * static_cast<int64_t>(d.height) > kMaxSourcePixels) {
      return Status::TooLarge;
    }

    if (hasLct && !loadColorTable(d, lctCount)) {
      return Status::Truncated;
    }
    if (d.paletteCount <= 0) {
      return Status::BadHeader;
    }

    const int minCodeSize = readU8(d);
    if (minCodeSize < 0) {
      return Status::Truncated;
    }

    DecodeInfo info;
    info.width = d.width;
    info.height = d.height;
    info.interlaced = d.interlaced;
    if (d.onBegin && !d.onBegin(d.cbCtx, info)) {
      return Status::DecodeError;
    }

    return decodeLzw(d, minCodeSize);
  }
}

int memRead(void* ctx, uint8_t* dst, int n) {
  auto* mem = static_cast<MemCursor*>(ctx);
  if (!mem || !mem->data || mem->pos >= mem->size || n <= 0) {
    return 0;
  }
  const size_t avail = mem->size - mem->pos;
  const size_t take = static_cast<size_t>(n) < avail ? static_cast<size_t>(n) : avail;
  memcpy(dst, mem->data + mem->pos, take);
  mem->pos += take;
  return static_cast<int>(take);
}

}  // namespace

Status decode(ByteSource src, BeginFn onBegin, RowFn onRow, void* ctx) {
  if (!heapguard::canAllocate(kPrefixBytes, heapguard::kCriticalFloorBytes)) {
    return Status::Oom;
  }
  auto prefix = makeUniqueNoThrow<uint16_t[]>(kLzwCodeCount);
  auto suffix = makeUniqueNoThrow<uint8_t[]>(kLzwCodeCount);
  auto stack = makeUniqueNoThrow<uint8_t[]>(kLzwCodeCount);
  auto palette = makeUniqueNoThrow<uint8_t[]>(kPaletteBytes);
  auto block = makeUniqueNoThrow<uint8_t[]>(kSubBlockBytes);
  auto line = makeUniqueNoThrow<uint8_t[]>(kMaxLineWidth);
  if (!prefix || !suffix || !stack || !palette || !block || !line) {
    return Status::Oom;
  }

  Decoder d{};
  d.src = src;
  d.onBegin = onBegin;
  d.onRow = onRow;
  d.cbCtx = ctx;
  d.prefix = prefix.get();
  d.suffix = suffix.get();
  d.stack = stack.get();
  d.palette = palette.get();
  d.block = block.get();
  d.line = line.get();
  return parseAndDecode(d);
}

Status decodeMemory(const uint8_t* data, size_t size, BeginFn onBegin, RowFn onRow, void* ctx) {
  if (!data) {
    return Status::BadHeader;
  }
  MemCursor mem{data, size, 0};
  ByteSource src{memRead, &mem};
  return decode(src, onBegin, onRow, ctx);
}

}  // namespace gifraster

#endif
