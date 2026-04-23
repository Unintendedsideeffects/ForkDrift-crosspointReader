#include "MarkdownPreprocessor.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace markdown::preprocess {
namespace {

std::string stripHeadingMarkup(const std::string& line, const uint8_t level) {
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
    if (!std::isspace(static_cast<unsigned char>(line[i]))) {
      return line;
    }
  }

  size_t trim = bracePos;
  while (trim > 0 && std::isspace(static_cast<unsigned char>(line[trim - 1]))) {
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
  const char c = line[i];
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
  return i < line.size() && line[i] == '|';
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

std::string formatCalloutLine(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    i++;
  }
  if (i >= line.size() || line[i] != '>') {
    return line;
  }
  size_t j = i + 1;
  while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
    j++;
  }
  if (j + 2 >= line.size() || line[j] != '[' || line[j + 1] != '!') {
    return line;
  }
  const size_t typeStart = j + 2;
  const size_t typeEnd = line.find(']', typeStart);
  if (typeEnd == std::string::npos) {
    return line;
  }
  const std::string type = line.substr(typeStart, typeEnd - typeStart);
  if (type.empty()) {
    return line;
  }

  size_t titleStart = typeEnd + 1;
  char foldState = '\0';
  if (titleStart < line.size() && (line[titleStart] == '-' || line[titleStart] == '+')) {
    foldState = line[titleStart];
    titleStart++;
  }
  while (titleStart < line.size() && (line[titleStart] == ' ' || line[titleStart] == '\t')) {
    titleStart++;
  }
  const std::string title = line.substr(titleStart);

  std::string out;
  out.reserve(line.size() + 8);
  out.append(line.substr(0, i + 1));
  out.append(" **");
  out.append(type);
  out.append("**");
  if (foldState == '-' || foldState == '+') {
    out.append(" [");
    out.push_back(foldState);
    out.append("]");
  }
  if (!title.empty()) {
    out.push_back(' ');
    out.append(title);
  }
  return out;
}

std::string processInline(const std::string& line) {
  std::string out;
  out.reserve(line.size());
  bool inCode = false;
  size_t codeFence = 0;

  auto isTagChar = [](const char c) -> bool {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
  };

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
      out.append(line.substr(i, tickCount));
      i += tickCount;
      continue;
    }

    if (inCode) {
      out.push_back(line[i]);
      i++;
      continue;
    }

    if (line[i] == '[' && i + 1 < line.size() && line[i + 1] == '^') {
      const size_t end = line.find(']', i + 2);
      if (end != std::string::npos && end > i + 2) {
        const std::string inner = line.substr(i + 2, end - (i + 2));
        if (inner.find(' ') == std::string::npos) {
          out.push_back('^');
          out.append(inner);
          out.push_back('^');
          i = end + 1;
          continue;
        }
      }
    }

    if (i + 2 < line.size() && line[i] == '!' && line[i + 1] == '[' && line[i + 2] == '[') {
      const size_t end = line.find("]]", i + 3);
      if (end != std::string::npos) {
        const std::string inner = line.substr(i + 3, end - (i + 3));
        std::string target = inner;
        std::string alias;
        const size_t pipePos = inner.find('|');
        if (pipePos != std::string::npos) {
          target = inner.substr(0, pipePos);
          alias = inner.substr(pipePos + 1);
        }
        target = trimSpaces(target);
        alias = trimSpaces(alias);
        if (hasImageExtension(target)) {
          int dimWidth = 0;
          int dimHeight = 0;
          const bool hasDims = !alias.empty() && parseDimensionToken(alias, dimWidth, dimHeight);
          std::string altText = hasDims ? fileStemFromPath(target) : alias;
          if (altText.empty()) {
            altText = fileStemFromPath(target);
          }
          out.append("![");
          out.append(altText);
          out.append("](");
          out.append(formatLinkTarget(target));
          if (hasDims) {
            out.append(" \"");
            out.append(alias);
            out.append("\"");
          }
          out.append(")");
        } else {
          std::string label = alias.empty() ? target : alias;
          std::string linkTarget = target;
          std::string baseTarget;
          if (stripBlockReferenceTarget(target, baseTarget) && !baseTarget.empty()) {
            linkTarget = baseTarget;
            if (alias.empty()) {
              label = baseTarget;
            }
          }
          out.append("[");
          out.append(label);
          out.append("](");
          out.append(formatLinkTarget(linkTarget));
          out.append(")");
        }
        i = end + 2;
        continue;
      }
    }

    if (i + 1 < line.size() && line[i] == '[' && line[i + 1] == '[') {
      const size_t end = line.find("]]", i + 2);
      if (end != std::string::npos) {
        const std::string inner = line.substr(i + 2, end - (i + 2));
        std::string target = inner;
        std::string alias;
        const size_t pipePos = inner.find('|');
        if (pipePos != std::string::npos) {
          target = inner.substr(0, pipePos);
          alias = inner.substr(pipePos + 1);
        }
        target = trimSpaces(target);
        alias = trimSpaces(alias);

        std::string label = alias.empty() ? target : alias;
        std::string linkTarget = target;
        std::string baseTarget;
        if (stripBlockReferenceTarget(target, baseTarget) && !baseTarget.empty()) {
          linkTarget = baseTarget;
          if (alias.empty()) {
            label = baseTarget;
          }
        }
        out.append("[");
        out.append(label);
        out.append("](");
        out.append(formatLinkTarget(linkTarget));
        out.append(")");
        i = end + 2;
        continue;
      }
    }

    if (i + 1 < line.size() && line[i] == '=' && line[i + 1] == '=') {
      const size_t end = line.find("==", i + 2);
      if (end != std::string::npos && end > i + 2) {
        const std::string inner = line.substr(i + 2, end - (i + 2));
        out.append("<mark>");
        out.append(inner);
        out.append("</mark>");
        i = end + 2;
        continue;
      }
    }

    if (line[i] == '#' && (i == 0 || !(std::isalnum(static_cast<unsigned char>(line[i - 1])) || line[i - 1] == '_'))) {
      size_t j = i + 1;
      if (j < line.size() && isTagChar(line[j])) {
        while (j < line.size() && isTagChar(line[j])) {
          j++;
        }
        out.append("*#");
        out.append(line.substr(i + 1, j - (i + 1)));
        out.append("*");
        i = j;
        continue;
      }
    }

    if (i + 1 < line.size() && line[i] == '~' && line[i + 1] == '~') {
      out.append("~~");
      i += 2;
      continue;
    }

    if (line[i] == '~' && i + 1 < line.size() && line[i + 1] != '~') {
      const size_t end = line.find('~', i + 1);
      if (end != std::string::npos && end > i + 1) {
        const std::string inner = line.substr(i + 1, end - (i + 1));
        if (inner.find(' ') == std::string::npos) {
          out.append("<sub>");
          out.append(inner);
          out.append("</sub>");
          i = end + 1;
          continue;
        }
      }
    }

    if (line[i] == '^') {
      const size_t end = line.find('^', i + 1);
      if (end != std::string::npos && end > i + 1) {
        const std::string inner = line.substr(i + 1, end - (i + 1));
        if (!inner.empty() && inner.find(' ') == std::string::npos) {
          out.append("<sup>");
          out.append(inner);
          out.append("</sup>");
          i = end + 1;
          continue;
        }
      }
    }

    out.push_back(line[i]);
    i++;
  }

  return out;
}

void appendLinesFromString(const std::string& text, std::vector<std::string>& outLines) {
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t end = text.find('\n', pos);
    const bool hasNewline = end != std::string::npos;
    const size_t len = hasNewline ? (end - pos) : (text.size() - pos);
    outLines.emplace_back(text.substr(pos, len));
    if (!hasNewline) {
      break;
    }
    pos = end + 1;
  }
}

}  // namespace

bool hasImageExtension(const std::string& target) {
  const auto dot = target.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = target.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
    if (std::isalnum(static_cast<unsigned char>(c))) {
      slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      prevHyphen = false;
    }
  }
  while (!slug.empty() && slug.back() == '-') {
    slug.pop_back();
  }
  return slug;
}

bool isHeadingLine(const std::string& line, uint8_t& outLevel, std::string& outText) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    i++;
  }
  const size_t hashStart = i;
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

std::string stripFrontmatter(const std::string& content) {
  size_t lineEnd = content.find('\n');
  if (lineEnd == std::string::npos) {
    return content;
  }

  const std::string firstLine = content.substr(0, lineEnd);
  if (firstLine != "---" && firstLine != "---\r") {
    return content;
  }

  size_t pos = lineEnd + 1;
  while (pos < content.size()) {
    lineEnd = content.find('\n', pos);
    const size_t len = (lineEnd == std::string::npos) ? content.size() - pos : lineEnd - pos;
    const std::string line = content.substr(pos, len);
    if (line == "---" || line == "---\r") {
      if (lineEnd == std::string::npos) {
        return "";
      }
      return content.substr(lineEnd + 1);
    }
    if (lineEnd == std::string::npos) {
      break;
    }
    pos = lineEnd + 1;
  }

  return content;
}

std::string stripComments(const std::string& content) {
  std::string output;
  output.reserve(content.size());
  size_t i = 0;
  while (i < content.size()) {
    if (i + 1 < content.size() && content[i] == '%' && content[i + 1] == '%') {
      const size_t end = content.find("%%", i + 2);
      if (end == std::string::npos) {
        break;
      }
      i = end + 2;
      continue;
    }
    output.push_back(content[i]);
    i++;
  }
  return output;
}

std::string processLine(const std::string& line) {
  std::string trimmed = line;
  if (!trimmed.empty() && trimmed.back() == '\r') {
    trimmed.pop_back();
  }
  std::string formatted = stripCustomHeadingId(trimmed);
  formatted = formatCalloutLine(formatted);
  formatted = processInline(formatted);
  formatted = stripBlockId(formatted);
  return formatted;
}

std::string stripBlockId(const std::string& line) {
  if (line.empty()) {
    return line;
  }

  const size_t caret = line.find_last_of('^');
  if (caret == std::string::npos || caret == 0) {
    return line;
  }
  if (!std::isspace(static_cast<unsigned char>(line[caret - 1]))) {
    return line;
  }

  size_t i = caret + 1;
  if (i >= line.size()) {
    return line;
  }

  while (i < line.size()) {
    const char c = line[i];
    if (c == ' ' || c == '\t' || c == '\r') {
      break;
    }
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
      return line;
    }
    i++;
  }

  size_t end = i;
  while (end < line.size() && (line[end] == ' ' || line[end] == '\t' || line[end] == '\r')) {
    end++;
  }

  size_t trim = caret - 1;
  while (trim > 0 && line[trim - 1] == ' ') {
    trim--;
  }
  return line.substr(0, trim) + line.substr(end);
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

std::string preprocessDocument(const std::string& content, const ExpandLineCallback& expandLine,
                               const size_t maxOutputBytes) {
  std::string processed = stripComments(stripFrontmatter(content));

  bool inFence = false;
  std::string fence;
  std::vector<std::string> lines;
  lines.reserve(256);

  size_t start = 0;
  while (start <= processed.size()) {
    const size_t end = processed.find('\n', start);
    const bool hasNewline = end != std::string::npos;
    const size_t lineLen = hasNewline ? (end - start) : (processed.size() - start);
    lines.emplace_back(processed.substr(start, lineLen));
    if (!hasNewline) {
      break;
    }
    start = end + 1;
  }

  const bool endsWithNewline = !processed.empty() && processed.back() == '\n';
  std::vector<std::string> outLines;
  outLines.reserve(lines.size() + 16);

  auto appendProcessedLine = [&](const std::string& line) {
    std::string expandedLine;
    if (expandLine && expandLine(line, expandedLine)) {
      appendLinesFromString(expandedLine, outLines);
    } else {
      outLines.push_back(processLine(line));
    }
  };

  for (size_t i = 0; i < lines.size();) {
    const std::string& line = lines[i];

    if (!inFence) {
      std::string newFence;
      if (isFenceStart(line, newFence)) {
        inFence = true;
        fence = newFence;
        outLines.push_back(line);
        i++;
        continue;
      }

      std::string caption;
      if (isTableCaptionLine(line, caption) && i + 1 < lines.size() && isTableLine(lines[i + 1])) {
        appendProcessedLine("*" + caption + "*");
        i++;
        continue;
      }

      std::string defIndent;
      std::string defText;
      if (i + 1 < lines.size() && isDefinitionTermCandidate(line) &&
          isDefinitionLine(lines[i + 1], defIndent, defText)) {
        size_t termIndentEnd = line.find_first_not_of(" \t");
        if (termIndentEnd == std::string::npos) {
          termIndentEnd = line.size();
        }
        const std::string termIndent = line.substr(0, termIndentEnd);
        const std::string termText = trimSpaces(line);
        appendProcessedLine(termIndent + "**" + termText + "**");
        i++;
        while (i < lines.size()) {
          std::string lineIndent;
          std::string lineDef;
          if (!isDefinitionLine(lines[i], lineIndent, lineDef)) {
            break;
          }
          std::string bulletIndent = lineIndent;
          if (bulletIndent.size() < termIndent.size()) {
            bulletIndent = termIndent;
          }
          appendProcessedLine(bulletIndent + "- " + lineDef);
          i++;
        }
        continue;
      }

      appendProcessedLine(line);
      i++;
      continue;
    }

    outLines.push_back(line);
    if (isFenceEnd(line, fence)) {
      inFence = false;
      fence.clear();
    }
    i++;
  }

  std::string output;
  size_t estimatedSize = 0;
  for (const auto& line : outLines) {
    estimatedSize += line.size() + 1;
  }
  output.reserve(estimatedSize);

  for (size_t i = 0; i < outLines.size(); i++) {
    output.append(outLines[i]);
    if (output.size() >= maxOutputBytes) {
      output.resize(maxOutputBytes);
      break;
    }
    if (i + 1 < outLines.size() || endsWithNewline) {
      output.push_back('\n');
    }
    if (output.size() >= maxOutputBytes) {
      output.resize(maxOutputBytes);
      break;
    }
  }

  return output;
}

}  // namespace markdown::preprocess
