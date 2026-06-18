#pragma once

#include <cstdint>
#include <string>

namespace OpdsFilename {

enum class Format : uint8_t {
  AuthorTitle = 0,
  TitleAuthor = 1,
};

std::string format(const std::string& title, const std::string& author, Format format, const std::string& extension);

}  // namespace OpdsFilename
