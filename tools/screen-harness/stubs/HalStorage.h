#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
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

  void setRoot(std::filesystem::path root);
  const std::filesystem::path& root() const { return root_; }
  bool hasRoot() const;

  bool mkdir(const char* path);
  bool openFileForWrite(const char* tag, const std::string& path, HalFile& file);
  bool openFileForRead(const char* tag, const std::string& path, HalFile& file);
  bool openFileForRead(const char* tag, const char* path, HalFile& file);
  bool exists(const char* path) const;
  bool remove(const char* path);
  bool writeFile(const char* path, const char* content);

 private:
  HalStorage();

  static std::string normalizePath(const std::string& path);
  std::filesystem::path hostPath(const std::string& sdPath) const;
  bool loadFromDisk(const std::string& sdPath, HalFile& file) const;
  void ensureDirectory(const std::string& path);
  void ensureParentDirs(const std::string& path);

  std::filesystem::path root_;
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> overlay_;
  std::set<std::string> directories_;
};

#define Storage HalStorage::getInstance()
