#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>

namespace BookCachePath {

inline uint32_t stableHash(const std::string& bookPath) {
  return std::accumulate(bookPath.begin(), bookPath.end(), 5381u,
                         [](uint32_t h, unsigned char ch) { return ((h << 5) + h) + ch; });
}

inline std::string build(const std::string& cacheDir, const char* prefix, const std::string& bookPath) {
  if (prefix == nullptr) {
    return "";
  }
  return cacheDir + "/" + prefix + std::to_string(stableHash(bookPath));
}

inline bool hasExtension(const std::string& bookPath, const char* extension) {
  const size_t extLen = std::strlen(extension);
  if (bookPath.size() < extLen) {
    return false;
  }

  const size_t start = bookPath.size() - extLen;
  for (size_t i = 0; i < extLen; ++i) {
    const unsigned char lhs = static_cast<unsigned char>(bookPath[start + i]);
    const unsigned char rhs = static_cast<unsigned char>(extension[i]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }

  return true;
}

inline const char* prefixForBookPath(const std::string& bookPath) {
  if (hasExtension(bookPath, ".epub")) {
    return "epub_";
  }
  if (hasExtension(bookPath, ".txt")) {
    return "txt_";
  }
  if (hasExtension(bookPath, ".md")) {
    return "md_";
  }
  if (hasExtension(bookPath, ".xtc") || hasExtension(bookPath, ".xtch")) {
    return "xtc_";
  }
  return nullptr;
}

inline bool resolve(const std::string& cacheDir, const std::string& bookPath, std::string& outCachePath) {
  const char* prefix = prefixForBookPath(bookPath);
  if (prefix == nullptr) {
    outCachePath.clear();
    return false;
  }

  outCachePath = build(cacheDir, prefix, bookPath);
  return true;
}

}  // namespace BookCachePath
