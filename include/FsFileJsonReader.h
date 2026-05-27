#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>

class FsFileJsonReader {
 public:
  explicit FsFileJsonReader(FsFile& file) : file_(file) {}

  int read() {
    uint8_t ch = 0;
    return file_.read(&ch, 1) == 1 ? static_cast<int>(ch) : -1;
  }

  size_t readBytes(char* buffer, size_t length) { return file_.read(reinterpret_cast<uint8_t*>(buffer), length); }

 private:
  FsFile& file_;
};
