#include "HalStorage.h"

#include <cstring>
#include <fstream>

void HalStorage::setRoot(std::filesystem::path root) { root_ = std::move(root); }

bool HalStorage::hasRoot() const { return !root_.empty() && std::filesystem::exists(root_); }

HalStorage::HalStorage() { directories_.insert("/"); }

std::string HalStorage::normalizePath(const std::string& path) {
  if (path.empty()) {
    return "/";
  }
  if (path.size() > 1 && path.back() == '/') {
    return path.substr(0, path.size() - 1);
  }
  return path;
}

std::filesystem::path HalStorage::hostPath(const std::string& sdPath) const {
  std::string rel = sdPath;
  if (!rel.empty() && rel.front() == '/') {
    rel.erase(0, 1);
  }
  return root_ / rel;
}

bool HalStorage::loadFromDisk(const std::string& sdPath, HalFile& file) const {
  if (!hasRoot()) {
    return false;
  }
  const auto path = hostPath(sdPath);
  if (!std::filesystem::is_regular_file(path)) {
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  auto buf = std::make_shared<std::vector<uint8_t>>();
  buf->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (buf->empty()) {
    return false;
  }
  file = HalFile::forRead(std::move(buf));
  return true;
}

void HalStorage::ensureDirectory(const std::string& path) {
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

void HalStorage::ensureParentDirs(const std::string& path) {
  const std::string normalized = normalizePath(path);
  const size_t slash = normalized.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    directories_.insert("/");
    return;
  }
  ensureDirectory(normalized.substr(0, slash));
}

bool HalStorage::mkdir(const char* path) {
  ensureDirectory(path != nullptr ? path : "/");
  if (hasRoot()) {
    const auto host = hostPath(path != nullptr ? path : "/");
    std::error_code ec;
    std::filesystem::create_directories(host, ec);
  }
  return true;
}

bool HalStorage::openFileForWrite(const char* /*tag*/, const std::string& path, HalFile& file) {
  ensureParentDirs(path);
  auto buf = std::make_shared<std::vector<uint8_t>>();
  overlay_[path] = buf;
  file = HalFile::forRead(buf);
  return true;
}

bool HalStorage::openFileForRead(const char* /*tag*/, const std::string& path, HalFile& file) {
  const auto overlayIt = overlay_.find(path);
  if (overlayIt != overlay_.end()) {
    file = HalFile::forRead(overlayIt->second);
    return true;
  }
  if (loadFromDisk(path, file)) {
    return true;
  }
  return false;
}

bool HalStorage::openFileForRead(const char* tag, const char* path, HalFile& file) {
  return openFileForRead(tag, std::string(path), file);
}

bool HalStorage::exists(const char* path) const {
  if (path == nullptr) {
    return false;
  }
  const std::string normalized = normalizePath(path);
  if (overlay_.count(normalized) > 0) {
    return true;
  }
  if (directories_.count(normalized) > 0) {
    return true;
  }
  if (hasRoot()) {
    const auto host = hostPath(normalized);
    return std::filesystem::exists(host);
  }
  return false;
}

bool HalStorage::remove(const char* path) {
  if (path == nullptr) {
    return false;
  }
  const std::string normalized = normalizePath(path);
  if (overlay_.erase(normalized) > 0) {
    return true;
  }
  return normalized != "/" && directories_.erase(normalized) > 0;
}
