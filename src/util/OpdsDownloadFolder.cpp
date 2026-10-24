#include "util/OpdsDownloadFolder.h"

#include <cstdint>

#include "util/PathUtils.h"

namespace {

bool isForbiddenFolderChar(const unsigned char c) {
  if (c < 32 || c == 127) {
    return true;
  }
  return std::string_view("\\<>:\"|?*").find(static_cast<char>(c)) != std::string_view::npos;
}

bool isValidUtf8(const std::string_view text) {
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t extra = 0;
    uint32_t minCp = 0;
    uint32_t cp = 0;
    if (c < 0x80) {
      ++i;
      continue;
    }
    if ((c >> 5) == 0x6) {
      extra = 1;
      cp = c & 0x1F;
      minCp = 0x80;
    } else if ((c >> 4) == 0xE) {
      extra = 2;
      cp = c & 0x0F;
      minCp = 0x800;
    } else if ((c >> 3) == 0x1E) {
      extra = 3;
      cp = c & 0x07;
      minCp = 0x10000;
    } else {
      return false;
    }
    if (i + extra >= text.size()) {
      return false;
    }
    for (size_t n = 1; n <= extra; ++n) {
      const unsigned char cont = static_cast<unsigned char>(text[i + n]);
      if ((cont >> 6) != 0x2) {
        return false;
      }
      cp = (cp << 6) | (cont & 0x3F);
    }
    if (cp < minCp || cp > 0x10FFFF) {
      return false;
    }
    i += extra + 1;
  }
  return true;
}

}  // namespace

namespace OpdsDownloadFolder {

bool normalize(std::string_view input, std::string& output) {
  while (!input.empty() && (input.front() == ' ' || input.front() == '\t')) {
    input.remove_prefix(1);
  }
  while (!input.empty() && (input.back() == ' ' || input.back() == '\t')) {
    input.remove_suffix(1);
  }
  if (input.size() > MAX_LENGTH) {
    return false;
  }
  if (!isValidUtf8(input)) {
    return false;
  }

  std::string folder;
  if (!input.empty()) {
    folder.reserve(input.size() + 1);
    folder = "/";
    for (const unsigned char c : input) {
      if (isForbiddenFolderChar(c)) {
        return false;
      }
      if (c != '/' || folder.back() != '/') {
        folder += static_cast<char>(c);
      }
    }
    if (folder.size() > 1 && folder.back() == '/') {
      folder.pop_back();
    }
    if (folder.size() > MAX_LENGTH) {
      return false;
    }
    for (size_t start = 1; start < folder.size();) {
      const size_t end = folder.find('/', start);
      const auto part =
          std::string_view(folder).substr(start, end == std::string::npos ? std::string_view::npos : end - start);
      if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
        return false;
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  }

  if (!folder.empty() && folder != "/" && PathUtils::containsTraversal(folder.c_str())) {
    return false;
  }

  output = std::move(folder);
  return true;
}

const char* effective(const std::string& overrideFolder, const char* defaultFolder) {
  return overrideFolder.empty() ? defaultFolder : overrideFolder.c_str();
}

}  // namespace OpdsDownloadFolder
