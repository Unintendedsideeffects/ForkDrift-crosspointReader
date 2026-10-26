#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <string>

// Books that just arrived (web/WebSocket/WebDAV upload, OPDS download) and
// should get one cover-thumbnail attempt. Fed from the web server task, drained
// once per book by the main loop. A failed attempt is not requeued: the only
// retry is opening the book on its first page (EpubReaderActivity).
class CoverArrivalQueue {
 public:
  static constexpr size_t kCapacity = 4;

  // Ignores non-EPUB paths and duplicates. When full, the oldest entry is dropped:
  // a bulk upload keeps its newest books, and the rest still get the first-open retry.
  void push(const std::string& path) {
    if (!isEpub(path)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count_; ++i) {
      if (at(i) == path) return;
    }
    if (count_ == kCapacity) {
      head_ = (head_ + 1) % kCapacity;
      --count_;
    }
    slots_[(head_ + count_) % kCapacity] = path;
    ++count_;
  }

  bool pop(std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == 0) return false;
    out = std::move(slots_[head_]);
    slots_[head_].clear();
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return true;
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_ == 0;
  }

  static CoverArrivalQueue& instance() {
    static CoverArrivalQueue queue;
    return queue;
  }

 private:
  static bool isEpub(const std::string& path) {
    if (path.size() < 5) return false;
    std::string ext = path.substr(path.size() - 5);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](const char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return ext == ".epub";
  }
  std::string& at(size_t i) { return slots_[(head_ + i) % kCapacity]; }

  mutable std::mutex mutex_;
  std::array<std::string, kCapacity> slots_;
  size_t head_ = 0;
  size_t count_ = 0;
};
