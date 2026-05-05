#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "String.h"

using oflag_t = int;
inline constexpr oflag_t O_RDONLY = 0;
inline constexpr oflag_t O_WRONLY = 1;

class HalStorage;

class HalFile {
  friend class HalStorage;

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&) noexcept;
  HalFile& operator=(HalFile&&) noexcept;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  size_t getName(char* name, size_t len) const;
  size_t size();
  size_t fileSize() { return size(); }
  bool seek(size_t pos) { return seekSet(pos); }
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b);
  bool rename(const char* newPath);
  bool getModifyDateTime(uint16_t*, uint16_t*) { return false; }
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  bool isOpen() const;
  operator bool() const;

 private:
  struct State;

  explicit HalFile(std::shared_ptr<State> state);

  std::shared_ptr<State> state_;
};

using FsFile = HalFile;

class HalStorage {
 public:
  friend class HalFile;

  HalStorage() = default;

  bool begin() { return configured_; }
  bool ready() const { return configured_; }
  bool setRoot(const std::string& rootPath);
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  String readFile(const char* path);
  bool readFileToStream(const char*, void*, size_t = 256) { return false; }
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  bool writeFile(const char* path, const String& content);
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  bool removeDir(const char* path) { return rmdir(path); }

  static HalStorage& getInstance();

 private:
  struct ResolvedPath {
    std::string logicalPath;
    std::filesystem::path hostPath;
  };

  bool isConfigured() const { return configured_; }
  bool resolvePath(const char* path, ResolvedPath& out, bool allowMissingLeaf = true) const;
  bool openFile(const char* path, std::ios::openmode mode, HalFile& file);
  std::vector<std::string> listDirectoryPaths(const std::string& logicalPath) const;

  bool configured_ = false;
  std::filesystem::path rootPath_;
};

#define Storage HalStorage::getInstance()
