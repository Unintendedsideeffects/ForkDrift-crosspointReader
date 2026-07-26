#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "MarkdownLimits.h"

namespace markdown::preprocess {

using ExpandLineCallback = std::function<bool(const std::string& line, std::string& expandedLine)>;

constexpr size_t kMaxPreprocessedOutputBytes = limits::kMaxPreprocessedBytes;

enum class PreprocessStatus : uint8_t {
  Ok,
  InputTooLarge,
  LineTooLong,
  OutputTooLarge,
};

struct PreprocessResult {
  PreprocessStatus status = PreprocessStatus::Ok;
  std::string output;

  explicit operator bool() const { return status == PreprocessStatus::Ok; }
};

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
PreprocessResult preprocessDocumentBounded(std::string content,
                                           const ExpandLineCallback& expandLine = ExpandLineCallback(),
                                           size_t maxOutputBytes = kMaxPreprocessedOutputBytes,
                                           size_t maxLineBytes = limits::kMaxLineBytes);

}  // namespace markdown::preprocess
