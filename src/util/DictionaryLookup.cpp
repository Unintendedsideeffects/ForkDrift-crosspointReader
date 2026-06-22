#include "DictionaryLookup.h"

#include <algorithm>
#include <vector>

std::string DictionaryLookup::caseFold(const std::string& input) {
  std::string result = input;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](char c) -> char { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; });
  return result;
}

std::string DictionaryLookup::lookup(DictionaryAccessor& accessor, const std::string& query) {
  size_t fileSize = accessor.size();
  if (fileSize == 0) {
    return "";
  }

  size_t lo = 0;
  size_t hi = fileSize;
  std::string foldedQuery = caseFold(query);

  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;

    // Find the start of the line containing 'mid' by scanning backwards
    size_t scanPos = mid;
    bool foundStart = false;
    while (scanPos > lo) {
      size_t readSize = std::min<size_t>(64, scanPos - lo);
      scanPos -= readSize;
      char buf[64];
      accessor.seek(scanPos);
      accessor.read(buf, readSize);

      for (int i = static_cast<int>(readSize) - 1; i >= 0; --i) {
        if (buf[i] == '\n') {
          scanPos += i + 1;
          foundStart = true;
          break;
        }
      }
      if (foundStart) break;
    }
    size_t lineStart = scanPos;

    // Read the current line
    std::string line;
    accessor.seek(lineStart);
    char buf[128];
    bool eolFound = false;
    size_t lineEnd = lineStart;

    while (!eolFound) {
      size_t readCount = accessor.read(buf, sizeof(buf));
      if (readCount == 0) {
        break;  // EOF
      }
      for (size_t i = 0; i < readCount; ++i) {
        if (buf[i] == '\n') {
          line.append(buf, i);
          lineEnd += i + 1;
          eolFound = true;
          break;
        }
      }
      if (!eolFound) {
        line.append(buf, readCount);
        lineEnd += readCount;
      }
    }

    // Parse headword and definition
    size_t tabPos = line.find('\t');
    std::string headword = (tabPos != std::string::npos) ? line.substr(0, tabPos) : line;
    if (!headword.empty() && headword.back() == '\r') {
      headword.pop_back();  // Just in case there's no tab but a \r
    }

    std::string foldedHead = caseFold(headword);
    int cmp = foldedHead.compare(foldedQuery);

    if (cmp == 0) {
      if (tabPos != std::string::npos) {
        std::string def = line.substr(tabPos + 1);
        if (!def.empty() && def.back() == '\r') {
          def.pop_back();
        }
        return def;
      }
      return "";  // No definition found
    } else if (cmp < 0) {
      lo = lineEnd;
    } else {
      hi = lineStart;
    }
  }

  return "";
}
