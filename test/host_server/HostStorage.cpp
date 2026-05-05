#include "HostStorage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

bool hasPathPrefix(const std::filesystem::path& path, const std::filesystem::path& prefix) {
  auto pathIt = path.begin();
  auto prefixIt = prefix.begin();
  for (; prefixIt != prefix.end(); ++prefixIt, ++pathIt) {
    if (pathIt == path.end() || *pathIt != *prefixIt) {
      return false;
    }
  }
  return true;
}

std::string normalizeLogicalPath(const char* rawPath) {
  std::filesystem::path logical;
  if (rawPath != nullptr) {
    for (const auto& part : std::filesystem::path(rawPath)) {
      const auto segment = part.string();
      if (segment.empty() || segment == "/") continue;
      if (segment == ".") continue;
      if (segment == "..") return {};
      logical /= part;
    }
  }

  const std::string normalized = logical.generic_string();
  return normalized.empty() ? "/" : "/" + normalized;
}

}  // namespace

struct HalFile::State {
  HalStorage* storage = nullptr;
  std::string logicalPath;
  std::filesystem::path hostPath;
  bool directory = false;
  std::fstream stream;
  std::vector<std::string> children;
  size_t childIndex = 0;
};

HalFile::HalFile() = default;
HalFile::~HalFile() { close(); }
HalFile::HalFile(std::shared_ptr<State> state) : state_(std::move(state)) {}
HalFile::HalFile(HalFile&& other) noexcept = default;
HalFile& HalFile::operator=(HalFile&& other) noexcept = default;

void HalFile::flush() {
  if (state_ && state_->stream.is_open()) {
    state_->stream.flush();
  }
}

size_t HalFile::getName(char* name, size_t len) const {
  if (!state_ || !name || len == 0) return 0;
  std::string value = std::filesystem::path(state_->logicalPath).filename().generic_string();
  if (value.empty() && state_->logicalPath == "/") value = "/";
  if (value.size() + 1 > len) return 0;
  std::memcpy(name, value.c_str(), value.size() + 1);
  return value.size();
}

size_t HalFile::size() {
  if (!state_ || state_->directory) return 0;
  flush();
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(state_->hostPath, ec);
  return ec ? 0 : static_cast<size_t>(fileSize);
}

bool HalFile::seekCur(int64_t offset) {
  if (!state_ || state_->directory || !state_->stream.is_open()) return false;
  state_->stream.clear();
  state_->stream.seekg(offset, std::ios::cur);
  state_->stream.seekp(offset, std::ios::cur);
  return !state_->stream.fail();
}

bool HalFile::seekSet(size_t offset) {
  if (!state_ || state_->directory || !state_->stream.is_open()) return false;
  state_->stream.clear();
  state_->stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  state_->stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
  return !state_->stream.fail();
}

int HalFile::available() const {
  if (!state_ || state_->directory || !state_->stream.is_open()) return 0;
  const auto current = state_->stream.tellg();
  if (current < 0) return 0;
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(state_->hostPath, ec);
  if (ec || fileSize <= static_cast<uintmax_t>(current)) return 0;
  return static_cast<int>(fileSize - static_cast<uintmax_t>(current));
}

size_t HalFile::position() const {
  if (!state_ || state_->directory || !state_->stream.is_open()) return 0;
  const auto pos = state_->stream.tellg();
  return pos < 0 ? 0 : static_cast<size_t>(pos);
}

int HalFile::read(void* buf, size_t count) {
  if (!state_ || state_->directory || !state_->stream.is_open() || !buf) return 0;
  state_->stream.read(static_cast<char*>(buf), static_cast<std::streamsize>(count));
  return static_cast<int>(state_->stream.gcount());
}

int HalFile::read() {
  unsigned char value = 0;
  return read(&value, 1) == 1 ? value : -1;
}

size_t HalFile::write(const void* buf, size_t count) {
  if (!state_ || state_->directory || !state_->stream.is_open() || !buf) return 0;
  state_->stream.write(static_cast<const char*>(buf), static_cast<std::streamsize>(count));
  return state_->stream.fail() ? 0 : count;
}

size_t HalFile::write(uint8_t b) { return write(&b, 1); }

bool HalFile::rename(const char* newPath) {
  if (!state_ || !state_->storage) return false;
  const std::string oldPath = state_->logicalPath;
  if (!state_->directory && state_->stream.is_open()) state_->stream.close();
  const bool ok = state_->storage->rename(oldPath.c_str(), newPath);
  if (!ok) return false;
  HalStorage::ResolvedPath resolved;
  if (!state_->storage->resolvePath(newPath, resolved)) return false;
  state_->logicalPath = resolved.logicalPath;
  state_->hostPath = resolved.hostPath;
  return true;
}

bool HalFile::isDirectory() const { return state_ && state_->directory; }

void HalFile::rewindDirectory() {
  if (state_ && state_->directory) state_->childIndex = 0;
}

bool HalFile::close() {
  if (!state_) return false;
  if (!state_->directory && state_->stream.is_open()) state_->stream.close();
  state_.reset();
  return true;
}

HalFile HalFile::openNextFile() {
  if (!state_ || !state_->directory || state_->childIndex >= state_->children.size() || !state_->storage) {
    return {};
  }
  return state_->storage->open(state_->children[state_->childIndex++].c_str());
}

bool HalFile::isOpen() const {
  if (!state_) return false;
  return state_->directory || state_->stream.is_open();
}

HalFile::operator bool() const { return isOpen(); }

bool HalStorage::setRoot(const std::string& rootPath) {
  std::error_code ec;
  auto absoluteRoot = std::filesystem::absolute(rootPath, ec);
  if (ec) return false;
  absoluteRoot = std::filesystem::weakly_canonical(absoluteRoot, ec);
  if (ec || !std::filesystem::exists(absoluteRoot) || !std::filesystem::is_directory(absoluteRoot)) {
    return false;
  }
  rootPath_ = absoluteRoot;
  configured_ = true;
  return true;
}

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  std::vector<String> files;
  if (maxFiles <= 0) return files;
  for (const auto& child : listDirectoryPaths(normalizeLogicalPath(path))) {
    files.emplace_back(child.c_str());
    if (static_cast<int>(files.size()) >= maxFiles) break;
  }
  return files;
}

String HalStorage::readFile(const char* path) {
  HalFile file;
  if (!openFileForRead("HOST", path, file)) return {};
  std::string content;
  content.resize(file.size());
  const int bytesRead = file.read(content.data(), content.size());
  file.close();
  if (bytesRead <= 0) return {};
  content.resize(static_cast<size_t>(bytesRead));
  return String(content);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  if (!buffer || bufferSize == 0) return 0;
  HalFile file;
  if (!openFileForRead("HOST", path, file)) {
    buffer[0] = '\0';
    return 0;
  }
  size_t limit = bufferSize - 1;
  if (maxBytes > 0) limit = std::min(limit, maxBytes);
  const int bytesRead = file.read(buffer, limit);
  const size_t written = bytesRead > 0 ? static_cast<size_t>(bytesRead) : 0;
  buffer[written] = '\0';
  file.close();
  return written;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HalFile file;
  if (!openFileForWrite("HOST", path, file)) return false;
  const size_t written = file.write(content.c_str(), content.length());
  file.close();
  return written == content.length();
}

bool HalStorage::ensureDirectoryExists(const char* path) { return mkdir(path, true); }

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved, oflag != O_RDONLY)) return {};

  std::error_code ec;
  if (std::filesystem::is_directory(resolved.hostPath, ec) && !ec) {
    auto state = std::make_shared<HalFile::State>();
    state->storage = this;
    state->logicalPath = resolved.logicalPath;
    state->hostPath = resolved.hostPath;
    state->directory = true;
    state->children = listDirectoryPaths(resolved.logicalPath);
    return HalFile(state);
  }

  auto state = std::make_shared<HalFile::State>();
  state->storage = this;
  state->logicalPath = resolved.logicalPath;
  state->hostPath = resolved.hostPath;

  std::ios::openmode mode = std::ios::binary;
  if (oflag == O_WRONLY) {
    mode |= std::ios::out | std::ios::trunc;
  } else {
    mode |= std::ios::in;
  }
  state->stream.open(resolved.hostPath, mode);
  return state->stream.is_open() ? HalFile(state) : HalFile();
}

bool HalStorage::mkdir(const char* path, bool pFlag) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved)) return false;
  std::error_code ec;
  if (pFlag) {
    std::filesystem::create_directories(resolved.hostPath, ec);
  } else {
    std::filesystem::create_directory(resolved.hostPath, ec);
  }
  return !ec && std::filesystem::exists(resolved.hostPath) && std::filesystem::is_directory(resolved.hostPath);
}

bool HalStorage::exists(const char* path) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved)) return false;
  return std::filesystem::exists(resolved.hostPath);
}

bool HalStorage::remove(const char* path) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved, false) || resolved.logicalPath == "/") return false;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(resolved.hostPath, ec)) return false;
  return std::filesystem::remove(resolved.hostPath, ec) && !ec;
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  ResolvedPath from;
  ResolvedPath to;
  if (!resolvePath(oldPath, from, false) || !resolvePath(newPath, to)) return false;
  std::error_code ec;
  if (!std::filesystem::exists(from.hostPath) || std::filesystem::exists(to.hostPath)) return false;
  std::filesystem::rename(from.hostPath, to.hostPath, ec);
  return !ec;
}

bool HalStorage::rmdir(const char* path) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved, false) || resolved.logicalPath == "/") return false;
  std::error_code ec;
  if (!std::filesystem::is_directory(resolved.hostPath, ec)) return false;
  return std::filesystem::remove(resolved.hostPath, ec) && !ec;
}

bool HalStorage::openFileForRead(const char*, const char* path, HalFile& file) {
  file = open(path, O_RDONLY);
  return file.isOpen() && !file.isDirectory();
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char*, const char* path, HalFile& file) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved)) return false;
  std::error_code ec;
  std::filesystem::create_directories(resolved.hostPath.parent_path(), ec);
  if (ec) return false;
  file = open(path, O_WRONLY);
  return file.isOpen() && !file.isDirectory();
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

HalStorage& HalStorage::getInstance() {
  static HalStorage instance;
  return instance;
}

bool HalStorage::resolvePath(const char* path, ResolvedPath& out, bool allowMissingLeaf) const {
  if (!configured_) return false;

  const std::string logical = normalizeLogicalPath(path);
  if (logical.empty()) return false;

  std::filesystem::path relative = logical == "/" ? std::filesystem::path{} : std::filesystem::path(logical.substr(1));
  const std::filesystem::path hostPath = (rootPath_ / relative).lexically_normal();

  std::error_code ec;
  const auto parent = hostPath.parent_path();
  const auto anchor = allowMissingLeaf ? parent : hostPath;
  const auto canonicalAnchor = std::filesystem::weakly_canonical(anchor.empty() ? rootPath_ : anchor, ec);
  if (ec || !hasPathPrefix(canonicalAnchor, rootPath_)) return false;

  if (!allowMissingLeaf && !std::filesystem::exists(hostPath)) return false;
  if (std::filesystem::exists(hostPath)) {
    const auto canonicalPath = std::filesystem::weakly_canonical(hostPath, ec);
    if (ec || !hasPathPrefix(canonicalPath, rootPath_)) return false;
  }

  out.logicalPath = logical;
  out.hostPath = hostPath;
  return true;
}

bool HalStorage::openFile(const char* path, std::ios::openmode mode, HalFile& file) {
  ResolvedPath resolved;
  if (!resolvePath(path, resolved, (mode & std::ios::out) != 0)) return false;
  auto state = std::make_shared<HalFile::State>();
  state->storage = this;
  state->logicalPath = resolved.logicalPath;
  state->hostPath = resolved.hostPath;
  state->stream.open(resolved.hostPath, mode | std::ios::binary);
  if (!state->stream.is_open()) return false;
  file = HalFile(state);
  return true;
}

std::vector<std::string> HalStorage::listDirectoryPaths(const std::string& logicalPath) const {
  std::vector<std::string> children;
  ResolvedPath resolved;
  if (!resolvePath(logicalPath.c_str(), resolved, false)) return children;

  std::error_code ec;
  if (!std::filesystem::is_directory(resolved.hostPath, ec) || ec) return children;

  for (const auto& entry : std::filesystem::directory_iterator(resolved.hostPath, ec)) {
    if (ec) break;
    const auto name = entry.path().filename().generic_string();
    if (name.empty()) continue;
    children.push_back(logicalPath == "/" ? "/" + name : logicalPath + "/" + name);
  }
  std::sort(children.begin(), children.end());
  return children;
}
