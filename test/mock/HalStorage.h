#pragma once
// Host test stub — replaces the real HalStorage/HalFile with an
// in-memory implementation suitable for unit testing.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Print.h"
#include "String.h"

using oflag_t = int;
constexpr oflag_t O_RDONLY = 0;
constexpr oflag_t O_WRONLY = 1;
constexpr oflag_t O_CREAT = 2;
constexpr oflag_t O_APPEND = 4;

// ── In-memory HalFile ────────────────────────────────────────────────────
class HalFile {
 public:
  HalFile() = default;

  static HalFile forWrite(std::shared_ptr<std::vector<uint8_t>> buf) {
    HalFile f;
    f.buf_ = buf;
    f.pos_ = 0;
    return f;
  }
  static HalFile forRead(std::shared_ptr<std::vector<uint8_t>> buf) { return forWrite(buf); }
  static HalFile forFile(std::string path, std::shared_ptr<std::vector<uint8_t>> buf) {
    HalFile f;
    f.path_ = std::move(path);
    f.buf_ = buf;
    f.pos_ = 0;
    return f;
  }
  static HalFile forDirectory(std::string path, std::vector<std::string> entries) {
    HalFile f;
    f.path_ = std::move(path);
    f.entries_ = std::move(entries);
    f.isDirectory_ = true;
    return f;
  }

  size_t write(const uint8_t* data, size_t len) {
    if (buf_) buf_->insert(buf_->end(), data, data + len);
    return len;
  }
  size_t write(uint8_t b) { return write(&b, 1); }

  size_t read(uint8_t* data, size_t len) {
    if (!buf_) return 0;
    const size_t avail = buf_->size() - pos_;
    const size_t n = std::min(len, avail);
    // An empty vector's data() may be nullptr, and memcpy's src is declared
    // nonnull — so memcpy(dst, nullptr, 0) is UB and trips UBSan even though it
    // copies nothing. Reading a zero-byte file is legitimate (see
    // sidecarIsOursOrAbsent), so guard rather than forbid it.
    if (n > 0) {
      std::memcpy(data, buf_->data() + pos_, n);
    }
    pos_ += n;
    return n;
  }
  size_t read(char* data, size_t len) { return read(reinterpret_cast<uint8_t*>(data), len); }
  size_t read(void* data, size_t len) { return read(reinterpret_cast<uint8_t*>(data), len); }
  int read() {
    if (!buf_ || pos_ >= buf_->size()) return -1;
    return static_cast<int>((*buf_)[pos_++]);
  }
  int available() const {
    if (!buf_ || pos_ > buf_->size()) return 0;
    return static_cast<int>(buf_->size() - pos_);
  }

  size_t size() const { return buf_ ? buf_->size() : 0; }
  uint64_t fileSize64() const { return static_cast<uint64_t>(size()); }
  bool isOpen() const { return buf_ != nullptr || isDirectory_; }

  bool seek(size_t pos) {
    if (!buf_ || pos > buf_->size()) return false;
    pos_ = pos;
    return true;
  }
  bool seek64(uint64_t pos) {
    if (pos > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false;
    return seek(static_cast<size_t>(pos));
  }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) {
    const int64_t newPos = static_cast<int64_t>(pos_) + offset;
    if (newPos < 0) return false;
    return seek(static_cast<size_t>(newPos));
  }
  size_t position() const { return pos_; }

  bool isDirectory() const { return isDirectory_; }

  bool getName(char* out, size_t len) const {
    if (!out || len == 0 || path_.empty()) return false;
    const size_t slash = path_.find_last_of('/');
    const std::string name = slash == std::string::npos ? path_ : path_.substr(slash + 1);
    if (name.size() + 1 > len) return false;
    std::memcpy(out, name.c_str(), name.size() + 1);
    return true;
  }

  HalFile openNextFile() {
    if (!isDirectory_ || entryIndex_ >= entries_.size()) return HalFile();
    return opener_ ? opener_(entries_[entryIndex_++]) : HalFile();
  }

  void setOpener(std::function<HalFile(const std::string&)> opener) { opener_ = std::move(opener); }

  void close() {}
  explicit operator bool() const { return buf_ != nullptr || isDirectory_; }

 private:
  std::shared_ptr<std::vector<uint8_t>> buf_;
  size_t pos_ = 0;
  std::string path_;
  std::vector<std::string> entries_;
  size_t entryIndex_ = 0;
  bool isDirectory_ = false;
  std::function<HalFile(const std::string&)> opener_;
};

using FsFile = HalFile;

// ── In-memory HalStorage singleton ──────────────────────────────────────
class HalStorage {
 public:
  HalStorage() { directories_.insert("/"); }

  bool mkdir(const char* path) {
    ensureDirectory(path ? path : "/");
    return true;
  }
  bool mkdir(const char* path, bool) { return mkdir(path); }

  bool openFileForWrite(const char* /*tag*/, const char* path, HalFile& file) {
    ensureParentDirs(path ? path : "");
    auto buf = std::make_shared<std::vector<uint8_t>>();
    files_[path] = buf;
    file = HalFile::forWrite(buf);
    return true;
  }
  bool openFileForWrite(const char* tag, const std::string& path, HalFile& file) {
    return openFileForWrite(tag, path.c_str(), file);
  }

  bool openFileForRead(const char* /*tag*/, const char* path, HalFile& file) {
    ++openFileForReadCount_;
    if (failNextReads_ > 0) {
      --failNextReads_;
      return false;
    }
    auto it = files_.find(path);
    if (it == files_.end()) return false;
    file = HalFile::forRead(it->second);
    return true;
  }
  bool openFileForRead(const char* tag, const std::string& path, HalFile& file) {
    return openFileForRead(tag, path.c_str(), file);
  }

  HalFile open(const char* path) { return open(path, O_RDONLY); }

  HalFile open(const char* path, const oflag_t flags) {
    const std::string normalized = normalizePath(path ? path : "");
    if ((flags & O_WRONLY) != 0) {
      auto it = files_.find(normalized);
      if (it == files_.end()) {
        if ((flags & O_CREAT) == 0) {
          return HalFile();
        }
        ensureParentDirs(normalized);
        files_[normalized] = std::make_shared<std::vector<uint8_t>>();
        it = files_.find(normalized);
      }
      HalFile file = HalFile::forFile(normalized, it->second);
      if ((flags & O_APPEND) != 0) {
        file.seek(it->second->size());
      }
      file.setOpener([this](const std::string& childPath) { return open(childPath.c_str()); });
      return file;
    }

    auto fileIt = files_.find(normalized);
    if (fileIt != files_.end()) {
      HalFile file = HalFile::forFile(normalized, fileIt->second);
      file.setOpener([this](const std::string& childPath) { return open(childPath.c_str()); });
      return file;
    }

    if (directories_.count(normalized) == 0) {
      return HalFile();
    }

    auto dir = HalFile::forDirectory(normalized, listDirectoryEntries(normalized));
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
    const size_t len = content.length();
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

  std::vector<String> listFiles(const char* path) const {
    std::vector<String> names;
    const std::string normalized = normalizePath(path ? path : "");
    const std::string prefix = normalized == "/" ? "/" : normalized + "/";
    std::set<std::string> seen;

    for (const auto& [filePath, _] : files_) {
      if (filePath.rfind(prefix, 0) != 0) continue;
      const std::string remainder = filePath.substr(prefix.size());
      if (remainder.empty() || remainder.find('/') != std::string::npos) continue;
      if (seen.insert(remainder).second) {
        names.emplace_back(remainder.c_str());
      }
    }

    return names;
  }

  static HalStorage& getInstance() {
    static HalStorage inst;
    return inst;
  }

  int openFileForReadCount() const { return openFileForReadCount_; }

  // Makes the next `n` calls to openFileForRead() fail regardless of whether
  // the file exists, simulating a slow SD card where a file written just
  // before is not yet visible to a fresh open. Used by tests that verify
  // bounded-retry-after-failure behavior (e.g. imagedims::probeFromFile).
  void failNextReads(int n) { failNextReads_ = n; }

  void reset() {
    files_.clear();
    directories_.clear();
    directories_.insert("/");
    openFileForReadCount_ = 0;
    failNextReads_ = 0;
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
  int openFileForReadCount_ = 0;
  int failNextReads_ = 0;
};

#define Storage HalStorage::getInstance()
