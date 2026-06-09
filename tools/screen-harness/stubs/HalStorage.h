#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using oflag_t = uint16_t;

class HalFile {
 public:
  HalFile() = default;

  static HalFile forRead(std::shared_ptr<std::vector<uint8_t>> buf) {
    HalFile file;
    file.buf_ = std::move(buf);
    return file;
  }

  size_t write(const uint8_t* data, const size_t len) {
    if (!buf_) {
      return 0;
    }
    buf_->insert(buf_->end(), data, data + len);
    return len;
  }

  size_t read(uint8_t* data, const size_t len) {
    if (!buf_) {
      return 0;
    }
    const size_t avail = buf_->size() - pos_;
    const size_t n = std::min(len, avail);
    std::memcpy(data, buf_->data() + pos_, n);
    pos_ += n;
    return n;
  }

  int read() {
    if (!buf_ || pos_ >= buf_->size()) {
      return -1;
    }
    return static_cast<int>((*buf_)[pos_++]);
  }

  uint64_t fileSize64() const { return buf_ ? buf_->size() : 0; }

  bool seek(const size_t pos) {
    if (!buf_ || pos > buf_->size()) {
      return false;
    }
    pos_ = pos;
    return true;
  }

  bool seek64(const uint64_t pos) { return seek(static_cast<size_t>(pos)); }

  bool seekCur(const int64_t offset) {
    const int64_t next = static_cast<int64_t>(pos_) + offset;
    if (next < 0) {
      return false;
    }
    return seek(static_cast<size_t>(next));
  }

  void close() { pos_ = 0; }

  explicit operator bool() const { return buf_ != nullptr; }

 private:
  std::shared_ptr<std::vector<uint8_t>> buf_;
  size_t pos_ = 0;
};

using FsFile = HalFile;

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool mkdir(const char* path) {
    ensureDirectory(path != nullptr ? path : "/");
    return true;
  }

  bool openFileForWrite(const char* /*tag*/, const std::string& path, HalFile& file) {
    ensureParentDirs(path);
    auto buf = std::make_shared<std::vector<uint8_t>>();
    files_[path] = buf;
    file = HalFile::forRead(buf);
    return true;
  }

  bool openFileForRead(const char* /*tag*/, const std::string& path, HalFile& file) {
    const auto it = files_.find(path);
    if (it == files_.end()) {
      return false;
    }
    file = HalFile::forRead(it->second);
    return true;
  }

  bool openFileForRead(const char* tag, const char* path, HalFile& file) {
    return openFileForRead(tag, std::string(path), file);
  }

  bool exists(const char* path) const {
    if (path == nullptr) {
      return false;
    }
    const std::string normalized = normalizePath(path);
    return files_.count(normalized) > 0 || directories_.count(normalized) > 0;
  }

  bool remove(const char* path) {
    if (path == nullptr) {
      return false;
    }
    const std::string normalized = normalizePath(path);
    if (files_.erase(normalized) > 0) {
      return true;
    }
    return normalized != "/" && directories_.erase(normalized) > 0;
  }

 private:
  HalStorage() { directories_.insert("/"); }

  static std::string normalizePath(const std::string& path) {
    if (path.empty()) {
      return "/";
    }
    if (path.size() > 1 && path.back() == '/') {
      return path.substr(0, path.size() - 1);
    }
    return path;
  }

  void ensureDirectory(const std::string& path) {
    const std::string normalized = normalizePath(path);
    if (normalized.empty()) {
      return;
    }
    directories_.insert(normalized);
    size_t pos = 1;
    while ((pos = normalized.find('/', pos)) != std::string::npos) {
      directories_.insert(normalizePath(normalized.substr(0, pos)));
      ++pos;
    }
  }

  void ensureParentDirs(const std::string& path) {
    const std::string normalized = normalizePath(path);
    const size_t slash = normalized.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
      directories_.insert("/");
      return;
    }
    ensureDirectory(normalized.substr(0, slash));
  }

  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files_;
  std::set<std::string> directories_;
};

#define Storage HalStorage::getInstance()
