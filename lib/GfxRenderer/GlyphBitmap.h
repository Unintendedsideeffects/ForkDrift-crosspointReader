#pragma once

#include <algorithm>
#include <cstdint>

namespace glyphBitmap {

enum class Plane : uint8_t { BW, GrayLSB, GrayMSB };

struct Frame {
  int x;
  int y;
  int dxX;
  int dxY;
  int dyX;
  int dyY;
};

struct Target {
  uint8_t* buffer;
  int width;
  int stride;
  int originY;
  int rows;
  Frame frame;
};

struct Clip {
  int left;
  int top;
  int right;
  int bottom;
};

inline void clipAxis(int base, int step, int lower, int upper, int& start, int& end) {
  if (step > 0) {
    start = std::max(start, lower - base);
    end = std::min(end, upper - base);
  } else {
    start = std::max(start, base - upper + 1);
    end = std::min(end, base - lower + 1);
  }
}

__attribute__((always_inline)) inline void clipToRect(const Frame& frame, int left, int top, int right, int bottom,
                                                      Clip& clip) {
  if (frame.dxX != 0) {
    clipAxis(frame.x, frame.dxX, left, right, clip.left, clip.right);
    clipAxis(frame.y, frame.dyY, top, bottom, clip.top, clip.bottom);
  } else {
    clipAxis(frame.x, frame.dyX, left, right, clip.top, clip.bottom);
    clipAxis(frame.y, frame.dxY, top, bottom, clip.left, clip.right);
  }
}

__attribute__((always_inline)) inline void paint(uint8_t* buffer, int destination, uint8_t ink, uint8_t levels,
                                                 bool clearBits) {
  if ((levels & (1u << ink)) == 0) return;
  const uint8_t mask = 0x80u >> (destination & 7);
  if (clearBits)
    buffer[destination >> 3] &= static_cast<uint8_t>(~mask);
  else
    buffer[destination >> 3] |= mask;
}

inline void draw(const uint8_t* bitmap, int width, int height, bool twoBit, Plane plane, bool state,
                 const Target& target, Clip clip) {
  uint8_t levels = 0x02;
  bool clearBits = state;
  if (twoBit) {
    levels = plane == Plane::BW ? 0x0e : plane == Plane::GrayMSB ? 0x06 : 0x04;
    if (plane != Plane::BW) clearBits = false;
  }

  clip.left = std::max(clip.left, 0);
  clip.top = std::max(clip.top, 0);
  clip.right = std::min(clip.right, width);
  clip.bottom = std::min(clip.bottom, height);
  clipToRect(target.frame, 0, target.originY, target.width, target.originY + target.rows, clip);
  if (clip.left >= clip.right || clip.top >= clip.bottom) return;

  const Frame& frame = target.frame;
  const int strideBits = target.stride * 8;
  const int stepX = frame.dxY * strideBits + frame.dxX;
  const int stepY = frame.dyY * strideBits + frame.dyX;
  int rowBit = (frame.y - target.originY) * strideBits + frame.x + clip.left * stepX + clip.top * stepY;
  for (int y = clip.top; y < clip.bottom; ++y, rowBit += stepY) {
    int source = y * width + clip.left;
    int destination = rowBit;
    int remaining = clip.right - clip.left;
    if (twoBit) {
      while (remaining && (source & 3)) {
        paint(target.buffer, destination, (bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3, levels, clearBits);
        ++source;
        destination += stepX;
        --remaining;
      }
      while (remaining >= 4) {
        const uint8_t packed = bitmap[source >> 2];
        paint(target.buffer, destination, packed >> 6, levels, clearBits);
        paint(target.buffer, destination + stepX, (packed >> 4) & 3, levels, clearBits);
        paint(target.buffer, destination + 2 * stepX, (packed >> 2) & 3, levels, clearBits);
        paint(target.buffer, destination + 3 * stepX, packed & 3, levels, clearBits);
        source += 4;
        destination += 4 * stepX;
        remaining -= 4;
      }
    }
    while (remaining--) {
      const uint8_t ink = twoBit ? ((bitmap[source >> 2] >> (6 - (source & 3) * 2)) & 3)
                                 : ((bitmap[source >> 3] >> (7 - (source & 7))) & 1);
      paint(target.buffer, destination, ink, levels, clearBits);
      ++source;
      destination += stepX;
    }
  }
}

}  // namespace glyphBitmap
