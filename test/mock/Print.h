#pragma once

// Host stand-in for Arduino's Print. Extracted from the HalDisplay mock so that
// headers reached through HalStorage (notably ZipFile.h, whose readFileToStream
// takes a Print&) can compile in the host test build without pulling in the
// display mock. #pragma once keeps a single definition when both are included.

#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t written = 0;
    while (written < size) {
      if (write(buffer[written]) == 0) break;
      written++;
    }
    return written;
  }
  // Arduino's Print declares flush(); OpdsParser overrides it. This shim now
  // shadows the Arduino header on the host include path, so it must carry the
  // same virtuals or every override becomes a compile error.
  virtual void flush() {}
};
