#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace markdown::preprocess {

using ExpandLineCallback = std::function<bool(const std::string& line, std::string& expandedLine)>;

constexpr size_t kMaxPreprocessedOutputBytes = 512 * 1024;

bool hasImageExtension(const std::string& target);
std::string trimSpaces(const std::string& value);
bool parseDimensionToken(const std::string& token, int& outWidth, int& outHeight);
std::string fileStemFromPath(const std::string& path);
bool stripBlockReferenceTarget(const std::string& target, std::string& outBase);
std::string formatLinkTarget(const std::string& target);
std::string normalizeSlug(const std::string& input);
bool isHeadingLine(const std::string& line, uint8_t& outLevel, std::string& outText);
std::string stripFrontmatter(const std::string& content);
std::string stripComments(const std::string& content);
std::string processLine(const std::string& line);
std::string stripBlockId(const std::string& line);
bool isFenceStart(const std::string& line, std::string& fence);
bool isFenceEnd(const std::string& line, const std::string& fence);
std::string preprocessDocument(const std::string& content, const ExpandLineCallback& expandLine = ExpandLineCallback(),
                               size_t maxOutputBytes = kMaxPreprocessedOutputBytes);

}  // namespace markdown::preprocess
