#pragma once
// Host test stub — replaces the real HalStorage/FsFile with an
// in-memory implementation suitable for unit testing.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "String.h"

// ── In-memory FsFile ─────────────────────────────────────────────────────
class FsFile {
 public:
  FsFile() = default;

  static FsFile forWrite(std::shared_ptr<std::vector<uint8_t>> buf) {
    FsFile f;
    f.buf_ = buf;
    f.pos_ = 0;
    return f;
  }
  static FsFile forRead(std::shared_ptr<std::vector<uint8_t>> buf) { return forWrite(buf); }
  static FsFile forFile(std::string path, std::shared_ptr<std::vector<uint8_t>> buf) {
    FsFile f;
    f.path_ = std::move(path);
    f.buf_ = buf;
    f.pos_ = 0;
    return f;
  }
  static FsFile forDirectory(std::string path, std::vector<std::string> entries) {
    FsFile f;
    f.path_ = std::move(path);
    f.entries_ = std::move(entries);
    f.isDirectory_ = true;
    return f;
  }

  size_t write(const uint8_t* data, size_t len) {
    if (buf_) buf_->insert(buf_->end(), data, data + len);
    return len;
  }

  size_t read(uint8_t* data, size_t len) {
    if (!buf_) return 0;
    const size_t avail = buf_->size() - pos_;
    const size_t n = std::min(len, avail);
    std::memcpy(data, buf_->data() + pos_, n);
    pos_ += n;
    return n;
  }

  size_t size() const { return buf_ ? buf_->size() : 0; }

  bool isDirectory() const { return isDirectory_; }

  bool getName(char* out, size_t len) const {
    if (!out || len == 0 || path_.empty()) return false;
    const size_t slash = path_.find_last_of('/');
    const std::string name = slash == std::string::npos ? path_ : path_.substr(slash + 1);
    if (name.size() + 1 > len) return false;
    std::memcpy(out, name.c_str(), name.size() + 1);
    return true;
  }

  FsFile openNextFile() {
    if (!isDirectory_ || entryIndex_ >= entries_.size()) return FsFile();
    return opener_ ? opener_(entries_[entryIndex_++]) : FsFile();
  }

  void setOpener(std::function<FsFile(const std::string&)> opener) { opener_ = std::move(opener); }

  void close() {}
  explicit operator bool() const { return buf_ != nullptr || isDirectory_; }

 private:
  std::shared_ptr<std::vector<uint8_t>> buf_;
  size_t pos_ = 0;
  std::string path_;
  std::vector<std::string> entries_;
  size_t entryIndex_ = 0;
  bool isDirectory_ = false;
  std::function<FsFile(const std::string&)> opener_;
};

// ── In-memory HalStorage singleton ──────────────────────────────────────
class HalStorage {
 public:
  HalStorage() { directories_.insert("/"); }

  bool mkdir(const char* path) {
    ensureDirectory(path ? path : "/");
    return true;
  }
  bool mkdir(const char* path, bool) { return mkdir(path); }

  bool openFileForWrite(const char* /*tag*/, const char* path, FsFile& file) {
    ensureParentDirs(path ? path : "");
    auto buf = std::make_shared<std::vector<uint8_t>>();
    files_[path] = buf;
    file = FsFile::forWrite(buf);
    return true;
  }
  bool openFileForWrite(const char* tag, const std::string& path, FsFile& file) {
    return openFileForWrite(tag, path.c_str(), file);
  }

  bool openFileForRead(const char* /*tag*/, const char* path, FsFile& file) {
    auto it = files_.find(path);
    if (it == files_.end()) return false;
    file = FsFile::forRead(it->second);
    return true;
  }
  bool openFileForRead(const char* tag, const std::string& path, FsFile& file) {
    return openFileForRead(tag, path.c_str(), file);
  }

  FsFile open(const char* path) {
    const std::string normalized = normalizePath(path ? path : "");
    auto fileIt = files_.find(normalized);
    if (fileIt != files_.end()) {
      FsFile file = FsFile::forFile(normalized, fileIt->second);
      file.setOpener([this](const std::string& childPath) { return open(childPath.c_str()); });
      return file;
    }

    if (directories_.count(normalized) == 0) {
      return FsFile();
    }

    auto dir = FsFile::forDirectory(normalized, listDirectoryEntries(normalized));
    dir.setOpener([this](const std::string& childPath) { return open(childPath.c_str()); });
    return dir;
  }

  String readFile(const char* path) {
    auto it = files_.find(path);
    if (it == files_.end() || it->second->empty()) return String();
    return String(std::string(reinterpret_cast<const char*>(it->second->data()), it->second->size()).c_str());
  }

  bool writeFile(const char* path, const String& content) {
    ensureParentDirs(path ? path : "");
    const char* s = content.c_str();
    const size_t len = std::strlen(s);
    auto buf = std::make_shared<std::vector<uint8_t>>(s, s + len);
    files_[path] = buf;
    return true;
  }

  bool rename(const char* oldPath, const char* newPath) {
    auto it = files_.find(oldPath);
    if (it == files_.end()) return false;
    ensureParentDirs(newPath ? newPath : "");
    files_[newPath] = std::move(it->second);
    files_.erase(it);
    return true;
  }

  // Unused in tests but referenced by CrossPointSettings.h surface
  bool exists(const char* path) {
    const std::string normalized = normalizePath(path ? path : "");
    return files_.count(normalized) > 0 || directories_.count(normalized) > 0;
  }
  bool remove(const char* path) {
    const std::string normalized = normalizePath(path ? path : "");
    if (files_.erase(normalized) > 0) return true;
    if (normalized != "/" && directories_.erase(normalized) > 0) return true;
    return false;
  }
  bool rmdir(const char* path) { return remove(path); }

  static HalStorage& getInstance() {
    static HalStorage inst;
    return inst;
  }

  // Reset between tests
  void reset() {
    files_.clear();
    directories_.clear();
    directories_.insert("/");
  }

 private:
  static std::string normalizePath(const std::string& path) {
    if (path.empty()) return "/";
    if (path.size() > 1 && path.back() == '/') return path.substr(0, path.size() - 1);
    return path;
  }

  void ensureDirectory(const std::string& path) {
    const std::string normalized = normalizePath(path);
    if (normalized.empty()) return;
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

  std::vector<std::string> listDirectoryEntries(const std::string& dirPath) const {
    std::set<std::string> uniqueEntries;
    const std::string prefix = dirPath == "/" ? "/" : dirPath + "/";

    for (const auto& [path, _] : files_) {
      if (path.rfind(prefix, 0) != 0) continue;
      const std::string remainder = path.substr(prefix.size());
      if (remainder.empty()) continue;
      const size_t slash = remainder.find('/');
      const std::string child = remainder.substr(0, slash);
      uniqueEntries.insert(prefix + child);
    }

    for (const auto& path : directories_) {
      if (path == dirPath || path.rfind(prefix, 0) != 0) continue;
      const std::string remainder = path.substr(prefix.size());
      if (remainder.empty()) continue;
      const size_t slash = remainder.find('/');
      const std::string child = remainder.substr(0, slash);
      uniqueEntries.insert(prefix + child);
    }

    return {uniqueEntries.begin(), uniqueEntries.end()};
  }

  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files_;
  std::set<std::string> directories_;
};

#define Storage HalStorage::getInstance()
