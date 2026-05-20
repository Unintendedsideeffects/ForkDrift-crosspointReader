#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

using oflag_t = uint16_t;

class FsFile {
 public:
  FsFile() = default;
  explicit operator bool() const { return false; }
  bool seek(uint32_t /*pos*/) { return false; }
  bool seekCur(int32_t /*offset*/) { return false; }
  int read() { return -1; }
  size_t read(void* /*buffer*/, size_t /*count*/) { return 0; }
  void close() {}
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char* /*tag*/, const std::string& /*path*/, FsFile& /*file*/) { return false; }
  bool openFileForRead(const char* /*tag*/, const char* /*path*/, FsFile& /*file*/) { return false; }
  bool exists(const char* /*path*/) const { return false; }
};

#define Storage HalStorage::getInstance()
