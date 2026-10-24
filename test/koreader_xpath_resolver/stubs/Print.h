#pragma once

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
  virtual void flush() {}
};
