#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// C++11 make_unique polyfill (md_detail::make_unique is C++14+)
// Uses nothrow to avoid abort() on OOM with -fno-exceptions
namespace md_detail {
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
}  // namespace md_detail

// Node type enumeration - covers CommonMark + GFM + Obsidian extensions
enum class MdNodeType : uint8_t {
  // Block nodes
  Document,
  Heading,
  Paragraph,
  CodeBlock,
  UnorderedList,
  OrderedList,
  ListItem,
  Blockquote,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableCell,
  HorizontalRule,
  HtmlBlock,

  // Inline nodes
  Text,
  SoftBreak,
  HardBreak,
  Emphasis,
  Strong,
  Link,
  Image,
  Code,
  Strikethrough,

  // Extensions (GFM/Obsidian)
  TaskListItem,
  WikiLink,
  Highlight,
  Subscript,
  Superscript,
  LatexMath,
  LatexMathDisplay,
};

// Text alignment for table cells
enum class MdAlign : uint8_t {
  Default,
  Left,
  Center,
  Right,
};

// Forward declaration
struct MdNode;

// Type-specific detail structures (avoid unions for clarity and safety)
struct MdHeadingDetail {
  uint8_t level = 1;  // 1-6
};

struct MdCodeBlockDetail {
  std::string language;
  std::string info;
  char fenceChar = '\0';  // '`' or '~', or '\0' for indented
};

struct MdListDetail {
  bool tight = false;
  char marker = '-';   // '-', '+', '*' for UL; '.' or ')' for OL
  uint32_t start = 1;  // Starting number for OL
};

struct MdListItemDetail {
  bool isTask = false;
  bool taskChecked = false;
};

struct MdTableDetail {
  uint16_t columnCount = 0;
  uint16_t headRowCount = 0;
  uint16_t bodyRowCount = 0;
};

struct MdTableCellDetail {
  MdAlign align = MdAlign::Default;
  bool isHeader = false;
};

struct MdLinkDetail {
  std::string href;
  std::string title;
  bool isAutolink = false;
};

struct MdImageDetail {
  std::string src;
  std::string title;
};

struct MdWikiLinkDetail {
  std::string target;
  std::string alias;  // Display text if different from target
};

// Main AST node structure
struct MdNode {
  static constexpr size_t MAX_PLAINTEXT_DEPTH = 50;

  MdNodeType type;
  std::vector<std::unique_ptr<MdNode>> children;

  // Text content (for Text, Code, HtmlBlock, LatexMath nodes)
  std::string text;

  // Type-specific details - use std::unique_ptr to avoid bloating all nodes
  std::unique_ptr<MdHeadingDetail> heading;
  std::unique_ptr<MdCodeBlockDetail> codeBlock;
  std::unique_ptr<MdListDetail> list;
  std::unique_ptr<MdListItemDetail> listItem;
  std::unique_ptr<MdTableDetail> table;
  std::unique_ptr<MdTableCellDetail> tableCell;
  std::unique_ptr<MdLinkDetail> link;
  std::unique_ptr<MdImageDetail> image;
  std::unique_ptr<MdWikiLinkDetail> wikiLink;

  explicit MdNode(MdNodeType t) : type(t) {}

  // Helper factory methods for common node types
  static std::unique_ptr<MdNode> createDocument() { return md_detail::make_unique<MdNode>(MdNodeType::Document); }

  static std::unique_ptr<MdNode> createHeading(uint8_t level) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::Heading);
    node->heading = md_detail::make_unique<MdHeadingDetail>();
    node->heading->level = level;
    return node;
  }

  static std::unique_ptr<MdNode> createParagraph() { return md_detail::make_unique<MdNode>(MdNodeType::Paragraph); }

  static std::unique_ptr<MdNode> createCodeBlock(const std::string& lang = "", char fence = '\0') {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::CodeBlock);
    node->codeBlock = md_detail::make_unique<MdCodeBlockDetail>();
    node->codeBlock->language = lang;
    node->codeBlock->fenceChar = fence;
    return node;
  }

  static std::unique_ptr<MdNode> createUnorderedList(bool tight, char marker) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::UnorderedList);
    node->list = md_detail::make_unique<MdListDetail>();
    node->list->tight = tight;
    node->list->marker = marker;
    return node;
  }

  static std::unique_ptr<MdNode> createOrderedList(bool tight, char marker, uint32_t start) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::OrderedList);
    node->list = md_detail::make_unique<MdListDetail>();
    node->list->tight = tight;
    node->list->marker = marker;
    node->list->start = start;
    return node;
  }

  static std::unique_ptr<MdNode> createListItem(bool isTask = false, bool checked = false) {
    auto node = md_detail::make_unique<MdNode>(isTask ? MdNodeType::TaskListItem : MdNodeType::ListItem);
    node->listItem = md_detail::make_unique<MdListItemDetail>();
    node->listItem->isTask = isTask;
    node->listItem->taskChecked = checked;
    return node;
  }

  static std::unique_ptr<MdNode> createBlockquote() { return md_detail::make_unique<MdNode>(MdNodeType::Blockquote); }

  static std::unique_ptr<MdNode> createTable(uint16_t cols, uint16_t headRows, uint16_t bodyRows) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::Table);
    node->table = md_detail::make_unique<MdTableDetail>();
    node->table->columnCount = cols;
    node->table->headRowCount = headRows;
    node->table->bodyRowCount = bodyRows;
    return node;
  }

  static std::unique_ptr<MdNode> createTableHead() { return md_detail::make_unique<MdNode>(MdNodeType::TableHead); }

  static std::unique_ptr<MdNode> createTableBody() { return md_detail::make_unique<MdNode>(MdNodeType::TableBody); }

  static std::unique_ptr<MdNode> createTableRow() { return md_detail::make_unique<MdNode>(MdNodeType::TableRow); }

  static std::unique_ptr<MdNode> createTableCell(MdAlign align, bool isHeader) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::TableCell);
    node->tableCell = md_detail::make_unique<MdTableCellDetail>();
    node->tableCell->align = align;
    node->tableCell->isHeader = isHeader;
    return node;
  }

  static std::unique_ptr<MdNode> createHorizontalRule() {
    return md_detail::make_unique<MdNode>(MdNodeType::HorizontalRule);
  }

  static std::unique_ptr<MdNode> createHtmlBlock(const std::string& html) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::HtmlBlock);
    node->text = html;
    return node;
  }

  static std::unique_ptr<MdNode> createText(const std::string& content) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::Text);
    node->text = content;
    return node;
  }

  static std::unique_ptr<MdNode> createSoftBreak() { return md_detail::make_unique<MdNode>(MdNodeType::SoftBreak); }

  static std::unique_ptr<MdNode> createHardBreak() { return md_detail::make_unique<MdNode>(MdNodeType::HardBreak); }

  static std::unique_ptr<MdNode> createEmphasis() { return md_detail::make_unique<MdNode>(MdNodeType::Emphasis); }

  static std::unique_ptr<MdNode> createStrong() { return md_detail::make_unique<MdNode>(MdNodeType::Strong); }

  static std::unique_ptr<MdNode> createLink(const std::string& href, const std::string& title = "",
                                            bool autolink = false) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::Link);
    node->link = md_detail::make_unique<MdLinkDetail>();
    node->link->href = href;
    node->link->title = title;
    node->link->isAutolink = autolink;
    return node;
  }

  static std::unique_ptr<MdNode> createImage(const std::string& src, const std::string& title = "") {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::Image);
    node->image = md_detail::make_unique<MdImageDetail>();
    node->image->src = src;
    node->image->title = title;
    return node;
  }

  static std::unique_ptr<MdNode> createCode(const std::string& content) {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::Code);
    node->text = content;
    return node;
  }

  static std::unique_ptr<MdNode> createStrikethrough() {
    return md_detail::make_unique<MdNode>(MdNodeType::Strikethrough);
  }

  static std::unique_ptr<MdNode> createWikiLink(const std::string& target, const std::string& alias = "") {
    auto node = md_detail::make_unique<MdNode>(MdNodeType::WikiLink);
    node->wikiLink = md_detail::make_unique<MdWikiLinkDetail>();
    node->wikiLink->target = target;
    node->wikiLink->alias = alias;
    return node;
  }

  static std::unique_ptr<MdNode> createHighlight() { return md_detail::make_unique<MdNode>(MdNodeType::Highlight); }

  static std::unique_ptr<MdNode> createSubscript() { return md_detail::make_unique<MdNode>(MdNodeType::Subscript); }

  static std::unique_ptr<MdNode> createSuperscript() { return md_detail::make_unique<MdNode>(MdNodeType::Superscript); }

  static std::unique_ptr<MdNode> createLatexMath(const std::string& content, bool display = false) {
    auto node = md_detail::make_unique<MdNode>(display ? MdNodeType::LatexMathDisplay : MdNodeType::LatexMath);
    node->text = content;
    return node;
  }

  // Utility methods
  void appendChild(std::unique_ptr<MdNode> child) { children.push_back(std::move(child)); }

  bool isBlock() const {
    switch (type) {
      case MdNodeType::Document:
      case MdNodeType::Heading:
      case MdNodeType::Paragraph:
      case MdNodeType::CodeBlock:
      case MdNodeType::UnorderedList:
      case MdNodeType::OrderedList:
      case MdNodeType::ListItem:
      case MdNodeType::Blockquote:
      case MdNodeType::Table:
      case MdNodeType::TableHead:
      case MdNodeType::TableBody:
      case MdNodeType::TableRow:
      case MdNodeType::TableCell:
      case MdNodeType::HorizontalRule:
      case MdNodeType::HtmlBlock:
      case MdNodeType::TaskListItem:
        return true;
      case MdNodeType::Text:
      case MdNodeType::SoftBreak:
      case MdNodeType::HardBreak:
      case MdNodeType::Emphasis:
      case MdNodeType::Strong:
      case MdNodeType::Link:
      case MdNodeType::Image:
      case MdNodeType::Code:
      case MdNodeType::Strikethrough:
      case MdNodeType::WikiLink:
      case MdNodeType::Highlight:
      case MdNodeType::Subscript:
      case MdNodeType::Superscript:
      case MdNodeType::LatexMath:
      case MdNodeType::LatexMathDisplay:
        return false;
    }
    return false;
  }

  bool isInline() const { return !isBlock(); }

  // Get plain text content (recursively extracts text from all descendants)
  std::string getPlainText() const {
    std::string result;
    collectPlainText(result, 0);
    return result;
  }

 private:
  void collectPlainText(std::string& out, size_t depth) const {
    if (depth > MAX_PLAINTEXT_DEPTH) {
      return;
    }
    if (type == MdNodeType::Text || type == MdNodeType::Code) {
      out += text;
    } else if (type == MdNodeType::SoftBreak || type == MdNodeType::HardBreak) {
      out += ' ';
    }
    for (const auto& child : children) {
      child->collectPlainText(out, depth + 1);
    }
  }
};
