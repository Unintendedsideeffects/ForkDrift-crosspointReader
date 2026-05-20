#include "MarkdownRenderer.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <FeatureFlags.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <ImageConverter.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"
#if ENABLE_BOOK_IMAGES
#include "Epub/converters/ImageDecoderFactory.h"
#endif

namespace {
class MemoryPrint : public Print {
  std::vector<uint8_t>& buffer;

 public:
  explicit MemoryPrint(std::vector<uint8_t>& buf) : buffer(buf) {}
  size_t write(uint8_t b) override {
    buffer.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    buffer.insert(buffer.end(), buf, buf + size);
    return size;
  }
};

bool decode1BitBmpToRaw(const std::vector<uint8_t>& bmpData, std::vector<uint8_t>& rawData, uint16_t& outWidth,
                        uint16_t& outHeight) {
  if (bmpData.size() < 54) {
    return false;
  }
  if (bmpData[0] != 'B' || bmpData[1] != 'M') {
    return false;
  }

  const uint32_t pixelOffset = bmpData[10] | (bmpData[11] << 8) | (bmpData[12] << 16) | (bmpData[13] << 24);
  const int32_t width = static_cast<int32_t>(bmpData[18]) | (static_cast<int32_t>(bmpData[19]) << 8) |
                        (static_cast<int32_t>(bmpData[20]) << 16) | (static_cast<int32_t>(bmpData[21]) << 24);
  int32_t height = static_cast<int32_t>(bmpData[22]) | (static_cast<int32_t>(bmpData[23]) << 8) |
                   (static_cast<int32_t>(bmpData[24]) << 16) | (static_cast<int32_t>(bmpData[25]) << 24);
  const bool topDown = height < 0;
  if (height < 0) {
    height = -height;
  }

  const uint16_t bitsPerPixel = bmpData[28] | (bmpData[29] << 8);
  if (bitsPerPixel != 1 || width <= 0 || height <= 0) {
    return false;
  }

  const uint32_t rowBytesBmp = ((static_cast<uint32_t>(width) + 31) / 32) * 4;
  const uint32_t rowBytesRaw = (static_cast<uint32_t>(width) + 7) / 8;
  const uint64_t requiredSize =
      static_cast<uint64_t>(pixelOffset) + static_cast<uint64_t>(rowBytesBmp) * static_cast<uint64_t>(height);
  if (requiredSize > bmpData.size()) {
    return false;
  }

  rawData.assign(rowBytesRaw * static_cast<uint32_t>(height), 0);
  for (int32_t row = 0; row < height; row++) {
    const uint32_t srcRow = static_cast<uint32_t>(topDown ? row : (height - 1 - row));
    const uint32_t srcOffset = pixelOffset + srcRow * rowBytesBmp;
    const uint32_t dstOffset = static_cast<uint32_t>(row) * rowBytesRaw;
    memcpy(rawData.data() + dstOffset, bmpData.data() + srcOffset, rowBytesRaw);
  }

  outWidth = static_cast<uint16_t>(width);
  outHeight = static_cast<uint16_t>(height);
  return true;
}

std::string trimWhitespace(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    start++;
  }
  if (start >= value.size()) {
    return "";
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    end--;
  }
  return value.substr(start, end - start);
}

bool parseDimensionToken(const std::string& token, int& outWidth, int& outHeight) {
  outWidth = 0;
  outHeight = 0;

  const std::string trimmed = trimWhitespace(token);
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

int hexToInt(const char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

std::string decodePercentEncoding(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = hexToInt(value[i + 1]);
      const int lo = hexToInt(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(value[i]);
  }
  return out;
}

std::string sanitizeImageSource(const std::string& raw) {
  std::string src = trimWhitespace(raw);
  if (src.size() >= 2 && src.front() == '<' && src.back() == '>') {
    src = src.substr(1, src.size() - 2);
  }
  const size_t queryPos = src.find('?');
  if (queryPos != std::string::npos) {
    src = src.substr(0, queryPos);
  }
  const size_t fragmentPos = src.find('#');
  if (fragmentPos != std::string::npos) {
    src = src.substr(0, fragmentPos);
  }
  return decodePercentEncoding(src);
}

bool extractAltDimensions(const std::string& altText, std::string& outAlt, int& outWidth, int& outHeight) {
  const size_t pipePos = altText.rfind('|');
  if (pipePos == std::string::npos) {
    return false;
  }
  const std::string dimToken = altText.substr(pipePos + 1);
  if (!parseDimensionToken(dimToken, outWidth, outHeight)) {
    return false;
  }
  outAlt = trimWhitespace(altText.substr(0, pipePos));
  return true;
}

std::vector<std::string> wrapTableCellText(const GfxRenderer& renderer, const int fontId, const std::string& rawText,
                                           const int maxWidth) {
  std::string text = trimWhitespace(rawText);
  if (text.empty()) {
    return {" "};
  }
  if (maxWidth <= 0) {
    return {text};
  }

  std::vector<std::string> words;
  words.reserve(8);
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
      i++;
    }
    const size_t start = i;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
      i++;
    }
    if (i > start) {
      words.emplace_back(text.substr(start, i - start));
    }
  }

  std::vector<std::string> lines;
  std::string currentLine;
  const auto pushCurrentLine = [&]() {
    if (!currentLine.empty()) {
      lines.push_back(currentLine);
      currentLine.clear();
    }
  };

  const auto pushBrokenWord = [&](const std::string& word) {
    std::string remaining = word;
    while (!remaining.empty()) {
      size_t splitPos = remaining.size();
      while (splitPos > 1 &&
             renderer.getTextWidth(fontId, remaining.substr(0, splitPos).c_str(), EpdFontFamily::REGULAR) > maxWidth) {
        splitPos--;
      }
      lines.push_back(remaining.substr(0, splitPos));
      remaining.erase(0, splitPos);
    }
  };

  for (const auto& word : words) {
    if (currentLine.empty()) {
      if (renderer.getTextWidth(fontId, word.c_str(), EpdFontFamily::REGULAR) <= maxWidth) {
        currentLine = word;
      } else {
        pushBrokenWord(word);
      }
      continue;
    }

    std::string candidate = currentLine + " " + word;
    if (renderer.getTextWidth(fontId, candidate.c_str(), EpdFontFamily::REGULAR) <= maxWidth) {
      currentLine = std::move(candidate);
    } else {
      pushCurrentLine();
      if (renderer.getTextWidth(fontId, word.c_str(), EpdFontFamily::REGULAR) <= maxWidth) {
        currentLine = word;
      } else {
        pushBrokenWord(word);
      }
    }
  }

  pushCurrentLine();
  if (lines.empty()) {
    lines.push_back(" ");
  }
  return lines;
}

CssTextAlign normalizeAlignment(const uint8_t style) {
  switch (static_cast<CssTextAlign>(style)) {
    case CssTextAlign::Justify:
    case CssTextAlign::Left:
    case CssTextAlign::Center:
    case CssTextAlign::Right:
      return static_cast<CssTextAlign>(style);
    case CssTextAlign::None:
    default:
      return CssTextAlign::Justify;
  }
}
}  // namespace

MarkdownRenderer::MarkdownRenderer(GfxRenderer& renderer, int fontId, int viewportWidth, int viewportHeight,
                                   float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                                   bool hyphenationEnabled, const std::string& contentBasePath)
    : renderer(renderer),
      fontId(fontId),
      viewportWidth(viewportWidth),
      viewportHeight(viewportHeight),
      lineCompression(lineCompression),
      extraParagraphSpacing(extraParagraphSpacing),
      paragraphAlignment(paragraphAlignment),
      hyphenationEnabled(hyphenationEnabled),
      contentBasePath(contentBasePath) {}

MarkdownRenderer::~MarkdownRenderer() = default;

bool MarkdownRenderer::render(const MdNode& root, const PageCallback& pageCallback,
                              const ProgressCallback& progressCallback) {
  // Initialize state
  onPageComplete = pageCallback;
  onProgress = progressCallback;
  currentPage.reset();
  currentTextBlock.reset();
  currentPageNextY = 0;
  pageCount = 0;
  currentDepth = 0;
  depthLimitExceeded = false;
  isBold = false;
  isItalic = false;
  isPreformatted = false;
  listDepth = 0;
  blockquoteDepth = 0;
  nodeToPage.clear();
  currentNodeIndex = 0;
  totalNodes = 0;
  lastProgress = -1;

  // Count total nodes for progress (rough estimate) - iterative to avoid stack overflow
  totalNodes = 0;
  {
    std::vector<const MdNode*> countStack;
    countStack.reserve(64);
    countStack.push_back(&root);
    while (!countStack.empty() && totalNodes < 100000) {  // Safety limit
      const MdNode* node = countStack.back();
      countStack.pop_back();
      totalNodes++;
      for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
        countStack.push_back(it->get());
      }
    }
  }

  // Reserve space for node mapping - bounded to avoid OOM
  nodeToPage.reserve(std::min(totalNodes, MAX_NODE_MAPPING));

  updateProgress();

  // Render the document
  renderNode(root);

  // Finalize any remaining content
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    flushTextBlock();
  }
  if (currentPage) {
    finalizePage();
  }

  return true;
}

void MarkdownRenderer::renderNode(const MdNode& node) {
  if (currentDepth >= MAX_RENDER_DEPTH) {
    if (!depthLimitExceeded) {
      LOG_WRN("MD", "Render depth limit exceeded (%zu)", MAX_RENDER_DEPTH);
      depthLimitExceeded = true;
    }
    return;
  }
  currentDepth++;

  // Record which page this node starts on - bounded to avoid OOM
  if (nodeToPage.size() < MAX_NODE_MAPPING) {
    nodeToPage.push_back(static_cast<size_t>(pageCount));
  }
  currentNodeIndex++;
  updateProgress();
  if (currentNodeIndex % 100 == 0) {
    esp_task_wdt_reset();
  }

  switch (node.type) {
    case MdNodeType::Document:
      renderDocument(node);
      break;
    case MdNodeType::Heading:
      renderHeading(node);
      break;
    case MdNodeType::Paragraph:
      renderParagraph(node);
      break;
    case MdNodeType::CodeBlock:
      renderCodeBlock(node);
      break;
    case MdNodeType::UnorderedList:
      renderUnorderedList(node);
      break;
    case MdNodeType::OrderedList:
      renderOrderedList(node);
      break;
    case MdNodeType::Blockquote:
      renderBlockquote(node);
      break;
    case MdNodeType::Table:
      renderTable(node);
      break;
    case MdNodeType::HorizontalRule:
      renderHorizontalRule(node);
      break;
    case MdNodeType::HtmlBlock:
      renderHtmlBlock(node);
      break;

    // Inline nodes - should be handled within block context
    case MdNodeType::Text:
      renderText(node);
      break;
    case MdNodeType::Emphasis:
      renderEmphasis(node);
      break;
    case MdNodeType::Strong:
      renderStrong(node);
      break;
    case MdNodeType::Link:
      renderLink(node);
      break;
    case MdNodeType::Image:
      renderImage(node);
      break;
    case MdNodeType::Code:
      renderCode(node);
      break;
    case MdNodeType::Strikethrough:
      renderStrikethrough(node);
      break;
    case MdNodeType::SoftBreak:
      renderSoftBreak(node);
      break;
    case MdNodeType::HardBreak:
      renderHardBreak(node);
      break;
    case MdNodeType::WikiLink:
      renderWikiLink(node);
      break;
    case MdNodeType::Highlight:
      renderHighlight(node);
      break;
    case MdNodeType::Subscript:
      renderSubscript(node);
      break;
    case MdNodeType::Superscript:
      renderSuperscript(node);
      break;
    case MdNodeType::LatexMath:
    case MdNodeType::LatexMathDisplay:
      renderLatexMath(node);
      break;

    // Container nodes that just recurse
    case MdNodeType::ListItem:
    case MdNodeType::TaskListItem:
    case MdNodeType::TableHead:
    case MdNodeType::TableBody:
    case MdNodeType::TableRow:
    case MdNodeType::TableCell:
      for (const auto& child : node.children) {
        renderNode(*child);
      }
      break;

    default:
      // Unknown node type - just render children
      for (const auto& child : node.children) {
        renderNode(*child);
      }
      break;
  }

  currentDepth--;
}

void MarkdownRenderer::renderDocument(const MdNode& node) {
  for (const auto& child : node.children) {
    renderNode(*child);
  }
}

void MarkdownRenderer::renderHeading(const MdNode& node) {
  flushTextBlock();

  // Headings are centered and bold
  startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
  isBold = true;

  renderInlineChildren(node);

  isBold = false;
  flushTextBlock();

  // Add extra spacing after headings
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentPageNextY += lineHeight / 2;
}

void MarkdownRenderer::renderParagraph(const MdNode& node) {
  flushTextBlock();
  startNewTextBlock(paragraphAlignment);

  // Add indent for blockquotes/lists
  std::string prefix = createIndentPrefix();
  if (!prefix.empty() && currentTextBlock) {
    currentTextBlock->addWord(prefix, EpdFontFamily::REGULAR);
  }

  renderInlineChildren(node);
  flushTextBlock();
}

void MarkdownRenderer::renderCodeBlock(const MdNode& node) {
  flushTextBlock();

  // Code blocks are left-aligned, no hyphenation, preserve structure
  isPreformatted = true;

  // Split code text into lines
  const std::string& code = node.text;
  size_t start = 0;
  while (start < code.size()) {
    size_t end = code.find('\n', start);
    if (end == std::string::npos) {
      end = code.size();
    }

    startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Left));
    std::string line = code.substr(start, end - start);

    // Add indent
    std::string prefix = createIndentPrefix();
    if (!prefix.empty()) {
      line = prefix + line;
    }

    if (line.empty()) {
      // Empty line - add a space to preserve the line
      currentTextBlock->addWord(" ", EpdFontFamily::REGULAR);
    } else {
      // Add the whole line as one "word" to preserve spacing
      currentTextBlock->addWord(line, EpdFontFamily::REGULAR);
    }
    flushTextBlock();

    start = end + 1;
  }

  isPreformatted = false;

  // Add spacing after code block
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentPageNextY += lineHeight / 2;
}

void MarkdownRenderer::renderUnorderedList(const MdNode& node) {
  flushTextBlock();
  listDepth++;

  int itemNumber = 1;
  for (const auto& child : node.children) {
    if (child->type == MdNodeType::ListItem || child->type == MdNodeType::TaskListItem) {
      renderListItem(*child, false, itemNumber++);
    } else {
      renderNode(*child);
    }
  }

  listDepth--;
}

void MarkdownRenderer::renderOrderedList(const MdNode& node) {
  flushTextBlock();
  listDepth++;

  int startNum = 1;
  if (node.list) {
    startNum = static_cast<int>(node.list->start);
  }

  int itemNumber = startNum;
  for (const auto& child : node.children) {
    if (child->type == MdNodeType::ListItem || child->type == MdNodeType::TaskListItem) {
      renderListItem(*child, true, itemNumber++);
    } else {
      renderNode(*child);
    }
  }

  listDepth--;
}

void MarkdownRenderer::renderListItem(const MdNode& node, bool ordered, int number) {
  startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Left));

  // Add indent based on list depth
  for (int i = 1; i < listDepth; i++) {
    currentTextBlock->addWord("  ", EpdFontFamily::REGULAR);
  }

  // Add bullet or number
  if (node.type == MdNodeType::TaskListItem && node.listItem) {
    // Task list item
    if (node.listItem->taskChecked) {
      currentTextBlock->addWord("[x]", EpdFontFamily::REGULAR);
    } else {
      currentTextBlock->addWord("[ ]", EpdFontFamily::REGULAR);
    }
  } else if (ordered) {
    currentTextBlock->addWord(std::to_string(number) + ".", EpdFontFamily::REGULAR);
  } else {
    // Bullet point (UTF-8 bullet character)
    currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR);
  }

  // Render item content
  for (const auto& child : node.children) {
    if (child->type == MdNodeType::Paragraph) {
      // Inline the paragraph content
      renderInlineChildren(*child);
    } else if (child->type == MdNodeType::UnorderedList || child->type == MdNodeType::OrderedList) {
      // Nested list
      flushTextBlock();
      renderNode(*child);
    } else {
      renderNode(*child);
    }
  }

  flushTextBlock();
}

void MarkdownRenderer::renderBlockquote(const MdNode& node) {
  flushTextBlock();
  blockquoteDepth++;

  for (const auto& child : node.children) {
    renderNode(*child);
  }

  blockquoteDepth--;
}

void MarkdownRenderer::renderTable(const MdNode& node) {
  flushTextBlock();

  // Get table dimensions
  uint16_t colCount = 0;
  if (node.table) {
    colCount = node.table->columnCount;
  }

  if (colCount == 0) {
    // Fallback: count columns from first row
    for (const auto& section : node.children) {
      for (const auto& row : section->children) {
        if (row->type == MdNodeType::TableRow) {
          colCount = static_cast<uint16_t>(row->children.size());
          break;
        }
      }
      if (colCount > 0) break;
    }
  }

  if (colCount == 0) {
    startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
    currentTextBlock->addWord("[Empty table]", EpdFontFamily::ITALIC);
    flushTextBlock();
    return;
  }

  // Calculate column width (equal distribution with small padding for readability).
  const int cellWidth = std::max(1, viewportWidth / static_cast<int>(colCount));
  const int wrappedCellWidth = std::max(1, cellWidth - renderer.getSpaceWidth(fontId));

  // Render table rows
  for (const auto& section : node.children) {
    if (section->type != MdNodeType::TableHead && section->type != MdNodeType::TableBody) {
      continue;
    }

    for (const auto& row : section->children) {
      if (row->type != MdNodeType::TableRow) {
        continue;
      }

      std::vector<std::vector<std::string>> wrappedCells;
      std::vector<EpdFontFamily::Style> cellStyles;
      wrappedCells.reserve(colCount);
      cellStyles.reserve(colCount);
      size_t rowHeight = 1;

      for (const auto& cell : row->children) {
        if (cell->type != MdNodeType::TableCell) {
          continue;
        }

        // Get cell text.
        std::string cellText = cell->getPlainText();
        EpdFontFamily::Style style = EpdFontFamily::REGULAR;
        if (cell->tableCell && cell->tableCell->isHeader) {
          style = EpdFontFamily::BOLD;
        }
        auto wrapped = wrapTableCellText(renderer, fontId, cellText, wrappedCellWidth);
        rowHeight = std::max(rowHeight, wrapped.size());
        wrappedCells.push_back(std::move(wrapped));
        cellStyles.push_back(style);
      }

      while (wrappedCells.size() < colCount) {
        wrappedCells.push_back({" "});
        cellStyles.push_back(EpdFontFamily::REGULAR);
      }

      for (size_t lineIdx = 0; lineIdx < rowHeight; lineIdx++) {
        startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Left));

        for (size_t col = 0; col < wrappedCells.size(); col++) {
          if (col > 0) {
            currentTextBlock->addWord("|", EpdFontFamily::REGULAR);
          }

          const auto& wrapped = wrappedCells[col];
          const std::string& lineText = lineIdx < wrapped.size() ? wrapped[lineIdx] : std::string(" ");
          currentTextBlock->addWord(lineText, cellStyles[col]);
        }

        flushTextBlock();
      }
    }

    // Add separator line after header
    if (section->type == MdNodeType::TableHead) {
      startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Left));
      // Cap column count to prevent unbounded string allocation
      constexpr uint16_t MAX_TABLE_COLS = 20;
      uint16_t safeCols = (colCount > MAX_TABLE_COLS) ? MAX_TABLE_COLS : colCount;
      std::string separator(safeCols * 8, '-');
      currentTextBlock->addWord(separator, EpdFontFamily::REGULAR);
      flushTextBlock();
    }
  }

  // Add spacing after table
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentPageNextY += lineHeight / 2;
}

void MarkdownRenderer::renderHorizontalRule(const MdNode& node) {
  (void)node;
  flushTextBlock();

  startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
  // Create a line of dashes
  std::string rule(40, '-');
  currentTextBlock->addWord(rule, EpdFontFamily::REGULAR);
  flushTextBlock();

  // Add spacing
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentPageNextY += lineHeight / 2;
}

void MarkdownRenderer::renderHtmlBlock(const MdNode& node) {
  // For now, just show HTML as-is in a code-like block
  if (!node.text.empty()) {
    flushTextBlock();
    startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Left));
    currentTextBlock->addWord("[HTML:", EpdFontFamily::ITALIC);
    currentTextBlock->addWord(node.text.substr(0, 50), EpdFontFamily::REGULAR);
    if (node.text.length() > 50) {
      currentTextBlock->addWord("...]", EpdFontFamily::ITALIC);
    } else {
      currentTextBlock->addWord("]", EpdFontFamily::ITALIC);
    }
    flushTextBlock();
  }
}

void MarkdownRenderer::renderInlineChildren(const MdNode& node) {
  for (const auto& child : node.children) {
    renderNode(*child);
  }
}

void MarkdownRenderer::renderText(const MdNode& node) {
  if (node.text.empty()) {
    return;
  }

  if (!currentTextBlock) {
    startNewTextBlock(paragraphAlignment);
  }

  // Split text into words
  const std::string& text = node.text;
  size_t start = 0;
  size_t i = 0;

  while (i <= text.size()) {
    bool isSpace = (i < text.size()) && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r');
    bool isEnd = (i == text.size());

    if (isSpace || isEnd) {
      if (i > start) {
        std::string word = text.substr(start, i - start);
        currentTextBlock->addWord(word, static_cast<EpdFontFamily::Style>(getCurrentFontStyle()));
      }
      start = i + 1;
    }
    i++;
  }
}

void MarkdownRenderer::renderEmphasis(const MdNode& node) {
  bool wasItalic = isItalic;
  isItalic = true;
  renderInlineChildren(node);
  isItalic = wasItalic;
}

void MarkdownRenderer::renderStrong(const MdNode& node) {
  bool wasBold = isBold;
  isBold = true;
  renderInlineChildren(node);
  isBold = wasBold;
}

void MarkdownRenderer::renderLink(const MdNode& node) {
  // Render link text (underlined not supported, so just render normally)
  renderInlineChildren(node);
}

void MarkdownRenderer::renderImage(const MdNode& node) {
  std::string src;
  int requestedWidth = 0;
  int requestedHeight = 0;
  if (node.image && !node.image->title.empty()) {
    parseDimensionToken(node.image->title, requestedWidth, requestedHeight);
  }

  std::string alt = trimWhitespace(node.getPlainText());
  if (requestedWidth == 0 && requestedHeight == 0) {
    std::string altWithoutDims;
    if (extractAltDimensions(alt, altWithoutDims, requestedWidth, requestedHeight)) {
      alt = altWithoutDims;
    }
  }
  if (alt.empty()) {
    alt = "Image";
  }
  if (node.image) {
    src = node.image->src;
  }
  src = sanitizeImageSource(src);

  if (src.empty()) {
    if (!currentTextBlock) {
      startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
    }
    currentTextBlock->addWord("[Image]", EpdFontFamily::ITALIC);
    return;
  }

  if (alt == "Image") {
    size_t lastSlash = src.find_last_of('/');
    if (lastSlash != std::string::npos) {
      alt = src.substr(lastSlash + 1);
    } else {
      alt = src;
    }
  }

  if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0) {
    if (!currentTextBlock) {
      startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
    }
    currentTextBlock->addWord("[Image: " + alt + "]", EpdFontFamily::ITALIC);
    return;
  }
  if (src.rfind("data:", 0) == 0) {
    if (!currentTextBlock) {
      startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
    }
    currentTextBlock->addWord("[Embedded image]", EpdFontFamily::ITALIC);
    return;
  }

#if ENABLE_BOOK_IMAGES
  // Resolve relative path against content base path
  std::string imagePath;
  if (!src.empty() && src[0] == '/') {
    imagePath = src;
  } else {
    imagePath = FsHelpers::normalisePath(contentBasePath + src);
  }

  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    flushTextBlock();
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  int maxWidth = viewportWidth;
  int maxHeight = viewportHeight / 2;
  if (requestedWidth > 0) {
    maxWidth = std::min(maxWidth, requestedWidth);
  }
  if (requestedHeight > 0) {
    maxHeight = std::min(maxHeight, requestedHeight);
  }
  if (maxWidth <= 0) {
    maxWidth = viewportWidth;
  }
  if (maxHeight <= 0) {
    maxHeight = viewportHeight / 2;
  }

  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(imagePath);
  if (!decoder) {
    startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
    currentTextBlock->addWord("[Image: " + alt + "]", EpdFontFamily::ITALIC);
    return;
  }

  ImageDimensions dims = {0, 0};
  if (!decoder->getDimensions(imagePath, dims) || dims.width <= 0 || dims.height <= 0) {
    startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
    currentTextBlock->addWord("[Image failed]", EpdFontFamily::ITALIC);
    return;
  }

  float scaleX = (dims.width > maxWidth) ? static_cast<float>(maxWidth) / static_cast<float>(dims.width) : 1.0f;
  float scaleY = (dims.height > maxHeight) ? static_cast<float>(maxHeight) / static_cast<float>(dims.height) : 1.0f;
  float scale = std::min(scaleX, scaleY);
  if (scale > 1.0f) {
    scale = 1.0f;
  }

  const int16_t displayWidth = static_cast<int16_t>(std::max(1, static_cast<int>(dims.width * scale)));
  const int16_t displayHeight = static_cast<int16_t>(std::max(1, static_cast<int>(dims.height * scale)));

  auto imageBlock = std::make_shared<ImageBlock>(imagePath, displayWidth, displayHeight);
  auto pageImage = std::make_shared<PageImage>(imageBlock, 0, 0);
  addImageToPage(pageImage);
#else
  (void)requestedWidth;
  (void)requestedHeight;
  if (!currentTextBlock) {
    startNewTextBlock(static_cast<uint8_t>(CssTextAlign::Center));
  }
  currentTextBlock->addWord("[Image: " + alt + "]", EpdFontFamily::ITALIC);
#endif
}

void MarkdownRenderer::renderCode(const MdNode& node) {
  if (!currentTextBlock) {
    startNewTextBlock(paragraphAlignment);
  }

  // Inline code - just add the text (monospace not available, so use regular)
  if (!node.text.empty()) {
    currentTextBlock->addWord("`" + node.text + "`", EpdFontFamily::REGULAR);
  }
}

void MarkdownRenderer::renderStrikethrough(const MdNode& node) {
  // Strikethrough not supported visually, just render children
  renderInlineChildren(node);
}

void MarkdownRenderer::renderSoftBreak(const MdNode& node) {
  (void)node;
  // Soft break = space
  if (currentTextBlock) {
    currentTextBlock->addWord(" ", static_cast<EpdFontFamily::Style>(getCurrentFontStyle()));
  }
}

void MarkdownRenderer::renderHardBreak(const MdNode& node) {
  (void)node;
  // Hard break = new line
  flushTextBlock();
  startNewTextBlock(paragraphAlignment);
}

void MarkdownRenderer::renderWikiLink(const MdNode& node) {
  if (!currentTextBlock) {
    startNewTextBlock(paragraphAlignment);
  }

  std::string text;
  if (node.wikiLink) {
    text = node.wikiLink->alias.empty() ? node.wikiLink->target : node.wikiLink->alias;
  }
  if (text.empty()) {
    text = node.getPlainText();
  }

  currentTextBlock->addWord(text, static_cast<EpdFontFamily::Style>(getCurrentFontStyle()));
}

void MarkdownRenderer::renderHighlight(const MdNode& node) {
  // Highlight not supported visually, render children with emphasis
  bool wasItalic = isItalic;
  isItalic = true;
  renderInlineChildren(node);
  isItalic = wasItalic;
}

void MarkdownRenderer::renderSubscript(const MdNode& node) {
  // Subscript not supported visually, render children with emphasis
  bool wasItalic = isItalic;
  isItalic = true;
  renderInlineChildren(node);
  isItalic = wasItalic;
}

void MarkdownRenderer::renderSuperscript(const MdNode& node) {
  // Superscript not supported visually, render children with emphasis
  bool wasItalic = isItalic;
  isItalic = true;
  renderInlineChildren(node);
  isItalic = wasItalic;
}

void MarkdownRenderer::renderLatexMath(const MdNode& node) {
  if (!currentTextBlock) {
    startNewTextBlock(paragraphAlignment);
  }

  // LaTeX not rendered, show as-is
  std::string mathText = node.text;
  if (node.type == MdNodeType::LatexMathDisplay) {
    currentTextBlock->addWord("$$" + mathText + "$$", EpdFontFamily::REGULAR);
  } else {
    currentTextBlock->addWord("$" + mathText + "$", EpdFontFamily::REGULAR);
  }
}

void MarkdownRenderer::startNewTextBlock(uint8_t style) {
  if (currentTextBlock && !currentTextBlock->isEmpty()) {
    flushTextBlock();
  }

  BlockStyle blockStyle;
  blockStyle.textAlignDefined = true;
  blockStyle.alignment = normalizeAlignment(style);
  currentTextBlock.reset(
      new ParsedText(extraParagraphSpacing, false, hyphenationEnabled && !isPreformatted, false, blockStyle, false));
}

void MarkdownRenderer::flushTextBlock() {
  if (!currentTextBlock || currentTextBlock->isEmpty()) {
    return;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int indentWidth = getIndentWidth();
  const int availableWidth = (viewportWidth > indentWidth) ? (viewportWidth - indentWidth) : 0;
  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, static_cast<uint16_t>(availableWidth),
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

  // Extra paragraph spacing if enabled
  if (extraParagraphSpacing) {
    const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
    currentPageNextY += lineHeight / 2;
  }

  currentTextBlock.reset();
}

void MarkdownRenderer::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

  if (currentPageNextY + lineHeight > viewportHeight) {
    finalizePage();
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  int xOffset = getIndentWidth();
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
  currentPageNextY += lineHeight;
}

void MarkdownRenderer::addImageToPage(std::shared_ptr<PageImage> image) {
  const int imageHeight = image->getHeight();

  if (currentPageNextY + imageHeight > viewportHeight) {
    finalizePage();
    currentPage.reset(new Page());
    currentPageNextY = 0;
  }

  const int16_t xPos = (viewportWidth - image->getWidth()) / 2;
  image->xPos = xPos;
  image->yPos = currentPageNextY;
  currentPage->elements.push_back(image);
  currentPageNextY += imageHeight;

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentPageNextY += lineHeight / 2;
}

void MarkdownRenderer::finalizePage() {
  if (currentPage && onPageComplete) {
    pageCount++;
    onPageComplete(std::move(currentPage));
  }
  currentPage.reset();
}

uint8_t MarkdownRenderer::getCurrentFontStyle() const {
  if (isBold && isItalic) {
    return EpdFontFamily::BOLD_ITALIC;
  } else if (isBold) {
    return EpdFontFamily::BOLD;
  } else if (isItalic) {
    return EpdFontFamily::ITALIC;
  }
  return EpdFontFamily::REGULAR;
}

int MarkdownRenderer::getIndentWidth() const {
  // Each blockquote level adds indent
  // Each list level (beyond first) adds indent
  int indent = 0;
  const int charWidth = 10;  // Approximate character width

  indent += blockquoteDepth * 2 * charWidth;

  return indent;
}

std::string MarkdownRenderer::createIndentPrefix() const {
  std::string prefix;

  // Add blockquote markers
  for (int i = 0; i < blockquoteDepth; i++) {
    prefix += "> ";
  }

  return prefix;
}

void MarkdownRenderer::updateProgress() {
  if (!onProgress || totalNodes == 0) {
    return;
  }
  int progress = static_cast<int>((currentNodeIndex * 100) / totalNodes);
  if (progress != lastProgress) {
    lastProgress = progress;
    onProgress(progress);
  }
}
