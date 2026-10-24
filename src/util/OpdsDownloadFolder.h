#pragma once

#include <string>
#include <string_view>

namespace OpdsDownloadFolder {

constexpr size_t MAX_LENGTH = 63;

bool normalize(std::string_view input, std::string& output);
const char* effective(const std::string& overrideFolder, const char* defaultFolder);

}  // namespace OpdsDownloadFolder
