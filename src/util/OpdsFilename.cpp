#include "OpdsFilename.h"

#include "StringUtils.h"

namespace {

std::string trimAscii(const std::string& value) {
  const size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string normalizeExtension(const std::string& extension) {
  std::string cleaned = trimAscii(extension);
  while (!cleaned.empty() && cleaned.front() == '.') {
    cleaned.erase(cleaned.begin());
  }
  return cleaned;
}

}  // namespace

namespace OpdsFilename {

std::string format(const std::string& title, const std::string& author, Format format, const std::string& extension) {
  const bool hasAuthor = !trimAscii(author).empty();
  const std::string cleanTitle = StringUtils::sanitizeFilename(title);
  const std::string cleanAuthor = StringUtils::sanitizeFilename(author);

  std::string base;
  if (!hasAuthor) {
    base = cleanTitle;
  } else if (format == Format::TitleAuthor) {
    base = cleanTitle + " - " + cleanAuthor;
  } else {
    base = cleanAuthor + " - " + cleanTitle;
  }

  base = StringUtils::sanitizeFilename(base);
  const std::string cleanExtension = normalizeExtension(extension);
  if (cleanExtension.empty()) {
    return base;
  }
  return base + "." + cleanExtension;
}

}  // namespace OpdsFilename
