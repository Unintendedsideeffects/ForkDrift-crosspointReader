#include "Markdown.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "MarkdownParser.h"
#include "MarkdownPreprocessor.h"

extern "C" {
#include <md4c-html.h>
}

namespace {
constexpr uint32_t META_MAGIC = 0x4D44544D;  // "MDTM"
constexpr uint8_t META_VERSION = 2;
constexpr int MAX_EMBED_DEPTH = 3;
constexpr size_t MAX_EMBED_BYTES = 256 * 1024;

struct SourceVersion {
  size_t fileSize = 0;
  uint16_t modifyDate = 0;
  uint16_t modifyTime = 0;
};

std::unordered_map<std::string, SourceVersion> knownBadParseFailures;

bool isHeadingLine(const std::string& line, uint8_t& outLevel, std::string& outText);

uint32_t hashFileContents(const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("MD ", path, file)) {
    return 0;
  }

  uint32_t hash = 2166136261u;  // FNV-1a
  uint8_t buffer[512];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    for (size_t i = 0; i < readSize; i++) {
      hash ^= buffer[i];
      hash *= 16777619u;
    }
  }
  file.close();
  return hash;
}

bool readSourceVersion(const std::string& path, SourceVersion& outVersion) {
  FsFile file;
  if (!Storage.openFileForRead("MD ", path, file)) {
    return false;
  }

  outVersion.fileSize = file.size();
  if (!file.getModifyDateTime(&outVersion.modifyDate, &outVersion.modifyTime)) {
    outVersion.modifyDate = 0;
    outVersion.modifyTime = 0;
  }
  file.close();
  return true;
}

bool sourceVersionMatches(const SourceVersion& lhs, const SourceVersion& rhs) {
  return lhs.fileSize == rhs.fileSize && lhs.modifyDate == rhs.modifyDate && lhs.modifyTime == rhs.modifyTime;
}

struct HtmlOutput {
  FsFile* file;
  bool ok;
};

void writeHtmlChunk(const MD_CHAR* data, MD_SIZE size, void* userdata) {
  auto* out = static_cast<HtmlOutput*>(userdata);
  if (!out || !out->ok || !out->file) {
    return;
  }
  if (out->file->write(reinterpret_cast<const uint8_t*>(data), size) != size) {
    out->ok = false;
  }
}

bool hasImageExtension(const std::string& target) {
  const auto dot = target.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = target.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "gif" || ext == "webp";
}

std::string trimSpaces(const std::string& value) {
  const size_t firstNonSpace = value.find_first_not_of(" \t");
  if (firstNonSpace == std::string::npos) {
    return "";
  }
  const size_t lastNonSpace = value.find_last_not_of(" \t");
  return value.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
}

bool parseDimensionToken(const std::string& token, int& outWidth, int& outHeight) {
  outWidth = 0;
  outHeight = 0;
  const std::string trimmed = trimSpaces(token);
  if (trimmed.empty()) {
    return false;
  }

  auto parseInt = [](const std::string& value, int& out) -> bool {
    if (value.empty()) {
      return false;
    }
    int result = 0;
    for (char c : value) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        return false;
      }
      result = (result * 10) + (c - '0');
    }
    out = result;
    return result > 0;
  };

  size_t xPos = trimmed.find('x');
  if (xPos == std::string::npos) {
    xPos = trimmed.find('X');
  }

  if (xPos == std::string::npos) {
    return parseInt(trimmed, outWidth);
  }

  const std::string widthToken = trimmed.substr(0, xPos);
  const std::string heightToken = trimmed.substr(xPos + 1);
  if (!parseInt(widthToken, outWidth)) {
    return false;
  }
  if (!heightToken.empty() && !parseInt(heightToken, outHeight)) {
    return false;
  }
  return true;
}

std::string fileStemFromPath(const std::string& path) {
  size_t start = 0;
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    start = lastSlash + 1;
  }
  size_t end = path.size();
  const size_t lastDot = path.find_last_of('.');
  if (lastDot != std::string::npos && lastDot > start) {
    end = lastDot;
  }
  return path.substr(start, end - start);
}

bool stripBlockReferenceTarget(const std::string& target, std::string& outBase) {
  const size_t hashCaret = target.find("#^");
  if (hashCaret != std::string::npos) {
    outBase = target.substr(0, hashCaret);
    return true;
  }
  const size_t caretPos = target.find('^');
  if (caretPos != std::string::npos) {
    outBase = target.substr(0, caretPos);
    return true;
  }
  return false;
}

std::string formatLinkTarget(const std::string& target) {
  if (target.find(' ') != std::string::npos) {
    return "<" + target + ">";
  }
  return target;
}

std::string stripCustomHeadingId(const std::string& line) {
  uint8_t level = 0;
  std::string text;
  if (!isHeadingLine(line, level, text)) {
    return line;
  }

  const size_t bracePos = line.rfind("{#");
  if (bracePos == std::string::npos) {
    return line;
  }
  const size_t endBrace = line.find('}', bracePos + 2);
  if (endBrace == std::string::npos) {
    return line;
  }
  for (size_t i = endBrace + 1; i < line.size(); i++) {
    if (!isspace(static_cast<unsigned char>(line[i]))) {
      return line;
    }
  }
  size_t trim = bracePos;
  while (trim > 0 && isspace(static_cast<unsigned char>(line[trim - 1]))) {
    trim--;
  }
  return line.substr(0, trim);
}

bool isDefinitionLine(const std::string& line, std::string& outIndent, std::string& outText) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i >= line.size() || line[i] != ':') {
    return false;
  }
  size_t start = i + 1;
  if (start < line.size() && line[start] == ' ') {
    start++;
  }
  outIndent = line.substr(0, i);
  outText = line.substr(start);
  return true;
}

bool isDefinitionTermCandidate(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i >= line.size()) {
    return false;
  }
  char c = line[i];
  if (c == '#' || c == '>' || c == '-' || c == '*' || c == '+' || c == '`' || c == '|' || c == ':') {
    return false;
  }
  return true;
}

bool isTableLine(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  return (i < line.size() && line[i] == '|');
}

bool isTableCaptionLine(const std::string& line, std::string& caption) {
  std::string trimmed = trimSpaces(line);
  if (trimmed.size() < 3) {
    return false;
  }
  if (trimmed.front() != '[' || trimmed.back() != ']') {
    return false;
  }
  caption = trimmed.substr(1, trimmed.size() - 2);
  return !caption.empty();
}

bool isFenceStart(const std::string& line, std::string& fence) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i + 2 >= line.size()) {
    return false;
  }
  const char ch = line[i];
  if (ch != '`' && ch != '~') {
    return false;
  }
  size_t count = 0;
  while (i + count < line.size() && line[i + count] == ch) {
    count++;
  }
  if (count < 3) {
    return false;
  }
  fence.assign(count, ch);
  return true;
}

bool isFenceEnd(const std::string& line, const std::string& fence) {
  if (fence.empty()) {
    return false;
  }
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i + fence.size() > line.size()) {
    return false;
  }
  for (size_t j = 0; j < fence.size(); j++) {
    if (line[i + j] != fence[j]) {
      return false;
    }
  }
  return true;
}

std::string normalizeSlug(const std::string& input) {
  std::string slug;
  bool prevHyphen = false;
  for (char c : input) {
    if (c == ' ' || c == '-' || c == '_') {
      if (!slug.empty() && !prevHyphen) {
        slug.push_back('-');
        prevHyphen = true;
      }
      continue;
    }
    if (isalnum(static_cast<unsigned char>(c))) {
      slug.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
      prevHyphen = false;
    }
  }
  while (!slug.empty() && slug.back() == '-') {
    slug.pop_back();
  }
  return slug;
}

bool readFileToString(const std::string& path, std::string& out, size_t maxBytes) {
  FsFile file;
  if (!Storage.openFileForRead("MD ", path, file)) {
    return false;
  }

  const size_t size = file.size();
  if (size == 0 || size > maxBytes) {
    file.close();
    return false;
  }

  out.clear();
  out.reserve(size + 1);
  uint8_t buffer[1024];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    out.append(reinterpret_cast<const char*>(buffer), readSize);
  }
  file.close();
  return true;
}

std::string stripHeadingMarkup(const std::string& line, uint8_t level) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    i++;
  }
  for (uint8_t c = 0; c < level && i < line.size(); c++) {
    if (line[i] == '#') {
      i++;
    }
  }
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  return line.substr(i);
}

bool isHeadingLine(const std::string& line, uint8_t& outLevel, std::string& outText) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    i++;
  }
  size_t hashStart = i;
  while (i < line.size() && line[i] == '#') {
    i++;
  }
  const size_t hashCount = i - hashStart;
  if (hashCount == 0 || hashCount > 6) {
    return false;
  }
  if (i < line.size() && line[i] != ' ' && line[i] != '\t') {
    return false;
  }
  outLevel = static_cast<uint8_t>(hashCount);
  outText = stripHeadingMarkup(line, outLevel);
  return true;
}

bool findBlockLine(const std::vector<std::string>& lines, const std::string& blockId, std::string& outLine) {
  if (blockId.empty()) {
    return false;
  }
  const std::string needle = "^" + blockId;
  for (const auto& line : lines) {
    const size_t pos = line.find(needle);
    if (pos == std::string::npos) {
      continue;
    }
    if (pos > 0) {
      char prev = line[pos - 1];
      if (prev != ' ' && prev != '\t') {
        continue;
      }
    }
    outLine = line;
    outLine = markdown::preprocess::stripBlockId(outLine);
    return true;
  }
  return false;
}

std::string extractSectionByHeading(const std::string& content, const std::string& heading) {
  if (heading.empty()) {
    return content;
  }

  std::vector<std::string> lines;
  lines.reserve(256);
  size_t start = 0;
  while (start <= content.size()) {
    const size_t end = content.find('\n', start);
    const bool hasNewline = end != std::string::npos;
    const size_t lineLen = hasNewline ? (end - start) : (content.size() - start);
    lines.emplace_back(content.substr(start, lineLen));
    if (!hasNewline) {
      break;
    }
    start = end + 1;
  }

  const std::string targetSlug = markdown::preprocess::normalizeSlug(heading);
  size_t startIndex = std::string::npos;
  uint8_t targetLevel = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    uint8_t level = 0;
    std::string text;
    if (!markdown::preprocess::isHeadingLine(lines[i], level, text)) {
      continue;
    }
    if (markdown::preprocess::normalizeSlug(text) == targetSlug) {
      startIndex = i;
      targetLevel = level;
      break;
    }
  }

  if (startIndex == std::string::npos) {
    return "";
  }

  size_t endIndex = lines.size();
  for (size_t i = startIndex + 1; i < lines.size(); i++) {
    uint8_t level = 0;
    std::string text;
    if (markdown::preprocess::isHeadingLine(lines[i], level, text) && level <= targetLevel) {
      endIndex = i;
      break;
    }
  }

  std::string out;
  for (size_t i = startIndex; i < endIndex; i++) {
    out.append(lines[i]);
    if (i + 1 < endIndex) {
      out.push_back('\n');
    }
  }
  return out;
}
}  // namespace

Markdown::Markdown(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/md_" + std::to_string(hash);
}

bool Markdown::load() {
  if (loaded) {
    return true;
  }

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("MD", "File does not exist: %s", filepath.c_str());
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("MD ", filepath, file)) {
    LOG_ERR("MD", "Failed to open file: %s", filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_INF("MD", "Loaded markdown file: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

std::string Markdown::getTitle() const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  if (filename.length() >= 3 && filename.substr(filename.length() - 3) == ".md") {
    filename = filename.substr(0, filename.length() - 3);
  }

  return filename;
}

void Markdown::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string Markdown::getHtmlPath() const { return cachePath + "/content.xhtml"; }

std::string Markdown::getContentBasePath() const {
  const auto lastSlash = filepath.find_last_of('/');
  if (lastSlash == std::string::npos) {
    return "/";
  }
  return filepath.substr(0, lastSlash + 1);
}

bool Markdown::shouldSkipKnownBadParseFailure() const {
  const auto it = knownBadParseFailures.find(filepath);
  if (it == knownBadParseFailures.end()) {
    return false;
  }

  SourceVersion currentVersion;
  if (!readSourceVersion(filepath, currentVersion)) {
    knownBadParseFailures.erase(it);
    return false;
  }

  if (!sourceVersionMatches(it->second, currentVersion)) {
    knownBadParseFailures.erase(it);
    return false;
  }

  return true;
}

void Markdown::markKnownBadParseFailure() const {
  SourceVersion currentVersion;
  if (readSourceVersion(filepath, currentVersion)) {
    knownBadParseFailures[filepath] = currentVersion;
  }
}

void Markdown::clearKnownBadParseFailure() const { knownBadParseFailures.erase(filepath); }

bool Markdown::ensureHtml() {
  if (!loaded) {
    return false;
  }

  if (shouldSkipKnownBadParseFailure()) {
    LOG_WRN("MD", "Skipping known-bad markdown source: %s", filepath.c_str());
    return false;
  }

  setupCacheDir();

  const std::string htmlPath = getHtmlPath();
  const std::string metaPath = cachePath + "/meta.bin";

  bool needsRender = true;
  if (Storage.exists(htmlPath.c_str()) && Storage.exists(metaPath.c_str())) {
    FsFile metaFile;
    if (Storage.openFileForRead("MD ", metaPath, metaFile)) {
      uint32_t magic = 0;
      uint8_t version = 0;
      uint32_t cachedSize = 0;
      uint32_t cachedHash = 0;
      serialization::readPod(metaFile, magic);
      serialization::readPod(metaFile, version);
      serialization::readPod(metaFile, cachedSize);
      serialization::readPod(metaFile, cachedHash);
      metaFile.close();
      if (magic == META_MAGIC && version == META_VERSION && cachedSize == fileSize && cachedHash != 0) {
        const uint32_t currentHash = hashFileContents(filepath);
        if (currentHash != 0 && currentHash == cachedHash) {
          needsRender = false;
        }
      }
    }
  }

  if (!needsRender) {
    return true;
  }

  if (!renderToHtmlFile(htmlPath)) {
    markKnownBadParseFailure();
    Storage.remove(htmlPath.c_str());
    return false;
  }

  FsFile metaFile;
  if (Storage.openFileForWrite("MD ", metaPath, metaFile)) {
    serialization::writePod(metaFile, META_MAGIC);
    serialization::writePod(metaFile, META_VERSION);
    serialization::writePod(metaFile, static_cast<uint32_t>(fileSize));
    serialization::writePod(metaFile, hashFileContents(filepath));
    metaFile.close();
  }

  return true;
}

bool Markdown::renderToHtmlFile(const std::string& htmlPath) const {
  FsFile file;
  if (!Storage.openFileForRead("MD ", filepath, file)) {
    return false;
  }

  std::string content;
  content.reserve(fileSize + 1);
  uint8_t buffer[1024];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    content.append(reinterpret_cast<const char*>(buffer), readSize);
  }
  file.close();

  std::vector<std::string> stack;
  stack.push_back(filepath);
  std::string output = preprocessContent(std::move(content), 0, stack);

  FsFile htmlFile;
  if (!Storage.openFileForWrite("MD ", htmlPath, htmlFile)) {
    return false;
  }

  HtmlOutput htmlOut{&htmlFile, true};
  constexpr char kHtmlOpen[] = "<html><body>\n";
  constexpr char kHtmlClose[] = "\n</body></html>";
  htmlFile.write(reinterpret_cast<const uint8_t*>(kHtmlOpen), sizeof(kHtmlOpen) - 1);

  const unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS;
  const unsigned rendererFlags = MD_HTML_FLAG_XHTML | MD_HTML_FLAG_SKIP_UTF8_BOM;
  const int result = md_html(output.c_str(), static_cast<MD_SIZE>(output.size()), writeHtmlChunk, &htmlOut, parserFlags,
                             rendererFlags);

  htmlFile.write(reinterpret_cast<const uint8_t*>(kHtmlClose), sizeof(kHtmlClose) - 1);
  htmlFile.close();

  return result == 0 && htmlOut.ok;
}

std::string Markdown::getContent() const {
  if (!loaded) {
    return "";
  }

  FsFile file;
  if (!Storage.openFileForRead("MD ", filepath, file)) {
    return "";
  }

  std::string content;
  content.reserve(fileSize + 1);
  uint8_t buffer[1024];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    content.append(reinterpret_cast<const char*>(buffer), readSize);
  }
  file.close();

  return content;
}

std::string Markdown::preprocessContent(std::string content, int depth, std::vector<std::string>& stack) const {
  if (depth > MAX_EMBED_DEPTH) {
    return "[Embedded note omitted]";
  }

  auto resolveEmbed = [&](const std::string& inner, std::string& expansion) -> bool {
    std::string target = inner;
    const size_t pipePos = target.find('|');
    if (pipePos != std::string::npos) {
      target = target.substr(0, pipePos);
    }

    std::string filePart = target;
    std::string heading;
    std::string blockId;
    const size_t hashPos = target.find('#');
    if (hashPos != std::string::npos) {
      filePart = target.substr(0, hashPos);
      std::string anchor = target.substr(hashPos + 1);
      if (!anchor.empty() && anchor[0] == '^') {
        blockId = anchor.substr(1);
      } else {
        heading = anchor;
      }
    } else {
      const size_t caretPos = target.find('^');
      if (caretPos != std::string::npos) {
        filePart = target.substr(0, caretPos);
        blockId = target.substr(caretPos + 1);
      }
    }

    if (filePart.empty() && heading.empty() && blockId.empty()) {
      return false;
    }

    const std::string fileTrimmed = markdown::preprocess::trimSpaces(filePart);
    if (!fileTrimmed.empty() && markdown::preprocess::hasImageExtension(fileTrimmed)) {
      return false;
    }

    std::string resolvedPath;
    std::string pathPart = fileTrimmed;
    if (pathPart.empty()) {
      resolvedPath = filepath;
    } else {
      size_t dot = pathPart.find_last_of('.');
      const size_t slash = pathPart.find_last_of('/');
      const bool hasExt = (dot != std::string::npos && (slash == std::string::npos || dot > slash));
      if (!hasExt) {
        pathPart += ".md";
      }

      if (!pathPart.empty() && pathPart[0] == '/') {
        resolvedPath = pathPart;
      } else {
        resolvedPath = FsHelpers::normalisePath(getContentBasePath() + pathPart);
      }
    }

    if (resolvedPath.empty()) {
      expansion = "[Embedded note not found]";
      return true;
    }

    std::string cycleKey = resolvedPath;
    if (!heading.empty()) {
      cycleKey += "#";
      cycleKey += heading;
    }
    if (!blockId.empty()) {
      cycleKey += "^";
      cycleKey += blockId;
    }

    if (std::find(stack.begin(), stack.end(), cycleKey) != stack.end()) {
      expansion = "[Embedded note omitted]";
      return true;
    }

    std::string embeddedContent;
    if (!readFileToString(resolvedPath, embeddedContent, MAX_EMBED_BYTES)) {
      expansion = "[Embedded note not found]";
      return true;
    }

    std::string selected = embeddedContent;
    if (!blockId.empty()) {
      std::vector<std::string> lines;
      lines.reserve(256);
      size_t s = 0;
      while (s <= embeddedContent.size()) {
        const size_t e = embeddedContent.find('\n', s);
        const bool nl = e != std::string::npos;
        const size_t len = nl ? (e - s) : (embeddedContent.size() - s);
        lines.emplace_back(embeddedContent.substr(s, len));
        if (!nl) {
          break;
        }
        s = e + 1;
      }
      std::string blockLine;
      if (findBlockLine(lines, blockId, blockLine)) {
        selected = blockLine;
      } else {
        selected.clear();
      }
    } else if (!heading.empty()) {
      selected = extractSectionByHeading(embeddedContent, heading);
    }

    if (selected.empty()) {
      expansion = "[Embedded note not found]";
      return true;
    }

    stack.push_back(cycleKey);
    expansion = preprocessContent(selected, depth + 1, stack);
    stack.pop_back();
    return true;
  };

  auto expandEmbedsInLine = [&](const std::string& line, std::string& expandedLine) -> bool {
    bool found = false;
    bool inCode = false;
    size_t codeFence = 0;
    size_t segmentStart = 0;
    bool pendingBreak = false;

    size_t i = 0;
    while (i < line.size()) {
      if (line[i] == '`') {
        size_t tickCount = 0;
        while (i + tickCount < line.size() && line[i + tickCount] == '`') {
          tickCount++;
        }
        if (!inCode) {
          inCode = true;
          codeFence = tickCount;
        } else if (tickCount == codeFence) {
          inCode = false;
          codeFence = 0;
        }
        i += tickCount;
        continue;
      }

      if (!inCode && i + 2 < line.size() && line[i] == '!' && line[i + 1] == '[' && line[i + 2] == '[') {
        const size_t end = line.find("]]", i + 3);
        if (end == std::string::npos) {
          i++;
          continue;
        }

        const std::string inner = line.substr(i + 3, end - (i + 3));
        std::string expansion;
        if (resolveEmbed(inner, expansion)) {
          found = true;
          if (i > segmentStart) {
            const std::string segment = markdown::preprocess::processLine(line.substr(segmentStart, i - segmentStart));
            if (segment.find_first_not_of(" \t\r") != std::string::npos) {
              if (pendingBreak && !expandedLine.empty() && expandedLine.back() != '\n') {
                expandedLine.push_back('\n');
              }
              expandedLine.append(segment);
            }
          }

          if (!expandedLine.empty() && expandedLine.back() != '\n') {
            expandedLine.push_back('\n');
          }
          expandedLine.append(expansion);
          pendingBreak = true;
          i = end + 2;
          segmentStart = i;
          continue;
        }

        i = end + 2;
        continue;
      }

      i++;
    }

    if (found && segmentStart < line.size()) {
      const std::string segment = markdown::preprocess::processLine(line.substr(segmentStart));
      if (segment.find_first_not_of(" \t\r") != std::string::npos) {
        if (pendingBreak && !expandedLine.empty() && expandedLine.back() != '\n') {
          expandedLine.push_back('\n');
        }
        expandedLine.append(segment);
      }
    }

    return found;
  };
  return markdown::preprocess::preprocessDocument(content, expandEmbedsInLine, MarkdownParser::MAX_INPUT_SIZE);
}

bool Markdown::parseToAst() {
  if (!loaded) {
    return false;
  }

  if (shouldSkipKnownBadParseFailure()) {
    LOG_WRN("MD", "Skipping known-bad markdown AST parse: %s", filepath.c_str());
    return false;
  }

  std::string content = getContent();
  if (content.empty()) {
    return false;
  }

  MarkdownParser parser;
  std::vector<std::string> stack;
  stack.push_back(filepath);
  std::string processed = preprocessContent(std::move(content), 0, stack);

  ast = parser.parse(processed);

  if (!ast) {
    LOG_ERR("MD", "Failed to parse markdown to AST");
    return false;
  }

  // Build navigation data from AST
  navigation = md_detail::make_unique<MarkdownNavigation>(*ast);

  LOG_INF("MD", "Parsed to AST: %zu TOC entries, %zu links", navigation->getTotalHeadings(),
          navigation->getTotalLinks());

  return true;
}
