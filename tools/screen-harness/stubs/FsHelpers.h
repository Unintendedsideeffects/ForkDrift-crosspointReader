#pragma once

#include <string_view>

namespace FsHelpers {

inline bool checkFileExtension(std::string_view fileName, const char* extension) {
  const std::string_view ext(extension);
  return fileName.size() >= ext.size() && fileName.substr(fileName.size() - ext.size()) == ext;
}

inline bool hasBmpExtension(std::string_view fileName) { return checkFileExtension(fileName, ".bmp"); }
inline bool hasEpubExtension(std::string_view fileName) { return checkFileExtension(fileName, ".epub"); }
inline bool hasXtcExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".xtc") || checkFileExtension(fileName, ".xtch");
}
inline bool hasTxtExtension(std::string_view fileName) { return checkFileExtension(fileName, ".txt"); }
inline bool hasMarkdownExtension(std::string_view fileName) { return checkFileExtension(fileName, ".md"); }

}  // namespace FsHelpers
