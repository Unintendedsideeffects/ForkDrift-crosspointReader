#pragma once

#include <string>

namespace stored_epub {

std::string select(const char* originalPath);
std::string prepare(const char* originalPath);
void invalidate(const char* originalPath);

}  // namespace stored_epub
