#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

class ScopedBuffer {
 public:
  explicit ScopedBuffer(size_t size) : ptr_(static_cast<uint8_t*>(std::malloc(size))), size_(size) {}

  ~ScopedBuffer() {
    if (ptr_ != nullptr) {
      std::free(ptr_);
    }
  }

  ScopedBuffer(const ScopedBuffer&) = delete;
  ScopedBuffer& operator=(const ScopedBuffer&) = delete;

  ScopedBuffer(ScopedBuffer&& other) noexcept
      : ptr_(std::exchange(other.ptr_, nullptr)), size_(std::exchange(other.size_, 0)) {}

  ScopedBuffer& operator=(ScopedBuffer&& other) noexcept {
    if (this != &other) {
      if (ptr_ != nullptr) {
        std::free(ptr_);
      }
      ptr_ = std::exchange(other.ptr_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  uint8_t* release() { return std::exchange(ptr_, nullptr); }

  uint8_t* data() { return ptr_; }
  const uint8_t* data() const { return ptr_; }

  size_t size() const { return size_; }

  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  uint8_t* ptr_;
  size_t size_;
};
