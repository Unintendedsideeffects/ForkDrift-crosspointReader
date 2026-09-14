#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class HalFile {
  FILE* file_ = nullptr;
  std::string path_;
  std::vector<std::string> children_;
  size_t child_ = 0;
  bool directory_ = false;

 public:
  inline static size_t reads = 0;
  HalFile() = default;
  explicit HalFile(std::string path) : path_(std::move(path)) {
    std::error_code ec;
    directory_ = std::filesystem::is_directory(path_, ec);
    if (directory_)
      for (const auto& child : std::filesystem::directory_iterator(path_)) children_.push_back(child.path());
    else
      file_ = std::fopen(path_.c_str(), "rb");
  }
  ~HalFile() { close(); }
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this != &other) {
      close();
      file_ = std::exchange(other.file_, nullptr);
      path_ = std::move(other.path_);
      children_ = std::move(other.children_);
      child_ = other.child_;
      directory_ = std::exchange(other.directory_, false);
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  int read(void* dest, size_t count) {
    ++reads;
    const size_t n = std::fread(dest, 1, count, file_);
    return std::ferror(file_) ? -1 : int(n);
  }
  size_t fileSize() const {
    std::error_code ec;
    return std::filesystem::file_size(path_, ec);
  }
  bool isDirectory() const { return directory_; }
  size_t getName(char* out, size_t length) const {
    const auto name = std::filesystem::path(path_).filename().string();
    std::snprintf(out, length, "%s", name.c_str());
    return name.size();
  }
  HalFile openNextFile() { return child_ < children_.size() ? HalFile(children_[child_++]) : HalFile(); }
  bool iterationFailed() const { return false; }
  bool close() {
    bool ok = true;
    if (file_) {
      ok = std::fclose(file_) == 0;
      file_ = nullptr;
    }
    directory_ = false;
    return ok;
  }
  explicit operator bool() const { return file_ || directory_; }
};
class HalStorage {
 public:
  inline static size_t opens = 0;
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  HalFile open(const char* path) {
    ++opens;
    const char* root = std::getenv("CROSSINK_TEST_SD");
    return HalFile(std::string(root ? root : ".") + path);
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file = open(path);
    return bool(file);
  }
};
#define Storage HalStorage::getInstance()
