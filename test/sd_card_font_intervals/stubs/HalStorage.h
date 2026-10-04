#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>

inline int failReadCall = -1, readCalls = 0, openFiles = 0;
inline unsigned long millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline struct {
  uint32_t getFreeHeap() const { return 1024 * 1024; }
  uint32_t getMaxAllocHeap() const { return 1024 * 1024; }
} ESP;
class HalFile {
  FILE* file_ = nullptr;

 public:
  ~HalFile() { close(); }
  bool open(const char* path) {
    close();
    file_ = std::fopen(path, "rb");
    if (file_) ++openFiles;
    return file_;
  }
  void close() {
    if (file_) {
      std::fclose(file_);
      --openFiles;
    }
    file_ = nullptr;
  }
  bool seekSet(uint32_t off) { return file_ && std::fseek(file_, off, SEEK_SET) == 0; }
  int read(void* dst, size_t n) {
    if (readCalls++ == failReadCall) return 0;
    return file_ ? static_cast<int>(std::fread(dst, 1, n, file_)) : 0;
  }
};
inline struct {
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path); }
} Storage;
