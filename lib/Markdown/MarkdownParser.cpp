#include "MarkdownParser.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>

#include "MarkdownPreprocessor.h"

// Helper to extract string from MD_ATTRIBUTE
static std::string attributeToString(const MD_ATTRIBUTE& attr) {
  if (attr.text == nullptr || attr.size == 0) {
    return "";
  }
  return std::string(attr.text, attr.size);
}

// Convert md4c alignment to our enum
static MdAlign convertAlign(MD_ALIGN align) {
  switch (align) {
    case MD_ALIGN_LEFT:
      return MdAlign::Left;
    case MD_ALIGN_CENTER:
      return MdAlign::Center;
    case MD_ALIGN_RIGHT:
      return MdAlign::Right;
    default:
      return MdAlign::Default;
  }
}

static bool hasImageExtension(const std::string& target) {
  const auto dot = target.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = target.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "gif" || ext == "webp";
}

static std::string formatLinkTarget(const std::string& target) {
  if (target.find(' ') != std::string::npos) {
    return "<" + target + ">";
  }
  return target;
}

static std::string encodeUtf8Codepoint(uint32_t cp) {
  std::string encoded;
  if (cp <= 0x7F) {
    encoded.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    encoded.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    encoded.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    encoded.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    encoded.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    encoded.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    encoded.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    encoded.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    encoded.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    encoded.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return encoded;
}

static bool parseNumericEntity(const std::string& content, uint32_t& codepoint) {
  if (content.size() <= 3 || content[0] != '&' || content[1] != '#' || content.back() != ';') {
    return false;
  }

  size_t index = 2;
  uint32_t base = 10;
  if (content[index] == 'x' || content[index] == 'X') {
    base = 16;
    index++;
  }
  if (index >= content.size() - 1) {
    return false;
  }

  uint32_t value = 0;
  for (; index < content.size() - 1; index++) {
    const unsigned char ch = static_cast<unsigned char>(content[index]);
    uint32_t digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = ch - '0';
    } else if (base == 16 && ch >= 'a' && ch <= 'f') {
      digit = 10 + (ch - 'a');
    } else if (base == 16 && ch >= 'A' && ch <= 'F') {
      digit = 10 + (ch - 'A');
    } else {
      return false;
    }

    if (value > (0x10FFFFu - digit) / base) {
      return false;
    }
    value = value * base + digit;
  }

  if (value > 0x10FFFFu) {
    return false;
  }
  codepoint = value;
  return true;
}

static std::string preprocessMarkdown(const std::string& input) {
  return markdown::preprocess::preprocessDocument(input);
}

std::unique_ptr<MdNode> MarkdownParser::parse(const std::string& markdown) {
  if (markdown.size() > MAX_INPUT_SIZE) {
    LOG_ERR("MD", "Parse failed: input size %zu exceeds limit %zu", markdown.size(), MAX_INPUT_SIZE);
    return nullptr;
  }

  const size_t freeHeap = ESP.getFreeHeap();
  constexpr size_t kHeapHeadroomBytes = 64 * 1024;
  if (freeHeap < markdown.size() + kHeapHeadroomBytes) {
    LOG_ERR("MD", "Parse failed: low heap (%zu free, need >= %zu)", freeHeap, markdown.size() + kHeapHeadroomBytes);
    return nullptr;
  }

  root = MdNode::createDocument();
  nodeStack.clear();
  nodeStack.push_back(root.get());
  nodeCount = 1;
  limitExceeded = false;

  MD_PARSER parser = {};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS;
  parser.enter_block = enterBlockCallback;
  parser.leave_block = leaveBlockCallback;
  parser.enter_span = enterSpanCallback;
  parser.leave_span = leaveSpanCallback;
  parser.text = textCallback;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  int result = md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, this);

  nodeStack.clear();

  if (result != 0 || limitExceeded) {
    root.reset();
    return nullptr;
  }

  return std::move(root);
}

std::unique_ptr<MdNode> MarkdownParser::parseWithPreprocessing(const std::string& markdown) {
  if (markdown.size() > MAX_INPUT_SIZE) {
    LOG_ERR("MD", "Preprocess failed: input size %zu exceeds limit %zu", markdown.size(), MAX_INPUT_SIZE);
    return nullptr;
  }
  std::string processed = ::preprocessMarkdown(markdown);
  return parse(processed);
}

MdNode* MarkdownParser::currentNode() {
  if (nodeStack.empty()) {
    return nullptr;
  }
  return nodeStack.back();
}

MdNode* MarkdownParser::pushNode(std::unique_ptr<MdNode> node) {
  MdNode* ptr = node.get();
  MdNode* parent = currentNode();
  const size_t nextDepth = nodeStack.size() + 1;
  if (!checkDepthLimit(nextDepth)) {
    return nullptr;
  }
  if (parent) {
    if (!appendChildNode(parent, std::move(node))) {
      return nullptr;
    }
  }
  nodeStack.push_back(ptr);
  return ptr;
}

void MarkdownParser::popNode() {
  if (!nodeStack.empty() && nodeStack.size() > 1) {
    nodeStack.pop_back();
  }
}

// Static callback trampolines
int MarkdownParser::enterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onEnterBlock(type, detail);
}

int MarkdownParser::leaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onLeaveBlock(type, detail);
}

int MarkdownParser::enterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onEnterSpan(type, detail);
}

int MarkdownParser::leaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onLeaveSpan(type, detail);
}

int MarkdownParser::textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onText(type, text, size);
}

int MarkdownParser::onEnterBlock(MD_BLOCKTYPE type, void* detail) {
  if (limitExceeded) {
    return -1;
  }
  switch (type) {
    case MD_BLOCK_DOC:
      // Document already created as root
      break;

    case MD_BLOCK_QUOTE:
      if (!pushNode(MdNode::createBlockquote())) {
        return -1;
      }
      break;

    case MD_BLOCK_UL: {
      auto* d = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
      if (!pushNode(MdNode::createUnorderedList(d->is_tight != 0, d->mark))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_OL: {
      auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
      if (!pushNode(MdNode::createOrderedList(d->is_tight != 0, d->mark_delimiter, d->start))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_LI: {
      auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
      if (!pushNode(MdNode::createListItem(d->is_task != 0, d->task_mark == 'x' || d->task_mark == 'X'))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_HR:
      if (!pushNode(MdNode::createHorizontalRule())) {
        return -1;
      }
      break;

    case MD_BLOCK_H: {
      auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      if (!pushNode(MdNode::createHeading(static_cast<uint8_t>(d->level)))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_CODE: {
      auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
      std::string lang = attributeToString(d->lang);
      if (!pushNode(MdNode::createCodeBlock(lang, d->fence_char))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_HTML:
      if (!pushNode(MdNode::createHtmlBlock(""))) {
        return -1;
      }
      break;

    case MD_BLOCK_P:
      if (!pushNode(MdNode::createParagraph())) {
        return -1;
      }
      break;

    case MD_BLOCK_TABLE: {
      auto* d = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
      if (!pushNode(MdNode::createTable(d->col_count, d->head_row_count, d->body_row_count))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_THEAD:
      if (!pushNode(MdNode::createTableHead())) {
        return -1;
      }
      break;

    case MD_BLOCK_TBODY:
      if (!pushNode(MdNode::createTableBody())) {
        return -1;
      }
      break;

    case MD_BLOCK_TR:
      if (!pushNode(MdNode::createTableRow())) {
        return -1;
      }
      break;

    case MD_BLOCK_TH: {
      auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
      if (!pushNode(MdNode::createTableCell(convertAlign(d->align), true))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_TD: {
      auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
      if (!pushNode(MdNode::createTableCell(convertAlign(d->align), false))) {
        return -1;
      }
      break;
    }

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onLeaveBlock(MD_BLOCKTYPE type, void* detail) {
  (void)detail;

  if (limitExceeded) {
    return -1;
  }

  switch (type) {
    case MD_BLOCK_DOC:
      // Don't pop root
      break;

    case MD_BLOCK_QUOTE:
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
    case MD_BLOCK_LI:
    case MD_BLOCK_HR:
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_HTML:
    case MD_BLOCK_P:
    case MD_BLOCK_TABLE:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
    case MD_BLOCK_TR:
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
      popNode();
      break;

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onEnterSpan(MD_SPANTYPE type, void* detail) {
  if (limitExceeded) {
    return -1;
  }
  switch (type) {
    case MD_SPAN_EM:
      if (!pushNode(MdNode::createEmphasis())) {
        return -1;
      }
      break;

    case MD_SPAN_STRONG:
      if (!pushNode(MdNode::createStrong())) {
        return -1;
      }
      break;

    case MD_SPAN_A: {
      auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
      if (!pushNode(MdNode::createLink(attributeToString(d->href), attributeToString(d->title), d->is_autolink != 0))) {
        return -1;
      }
      break;
    }

    case MD_SPAN_IMG: {
      auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      if (!pushNode(MdNode::createImage(attributeToString(d->src), attributeToString(d->title)))) {
        return -1;
      }
      break;
    }

    case MD_SPAN_CODE:
      if (!pushNode(MdNode::createCode(""))) {
        return -1;
      }
      break;

    case MD_SPAN_DEL:
      if (!pushNode(MdNode::createStrikethrough())) {
        return -1;
      }
      break;

    case MD_SPAN_LATEXMATH:
      if (!pushNode(MdNode::createLatexMath("", false))) {
        return -1;
      }
      break;

    case MD_SPAN_LATEXMATH_DISPLAY:
      if (!pushNode(MdNode::createLatexMath("", true))) {
        return -1;
      }
      break;

    case MD_SPAN_WIKILINK: {
      auto* d = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail);
      std::string target = attributeToString(d->target);
      std::string alias;
      const size_t pipePos = target.find('|');
      if (pipePos != std::string::npos) {
        alias = target.substr(pipePos + 1);
        target = target.substr(0, pipePos);
      }
      if (!pushNode(MdNode::createWikiLink(target, alias))) {
        return -1;
      }
      break;
    }

    case MD_SPAN_U:
      // Treat underline as emphasis for now
      if (!pushNode(MdNode::createEmphasis())) {
        return -1;
      }
      break;

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onLeaveSpan(MD_SPANTYPE type, void* detail) {
  (void)detail;

  if (limitExceeded) {
    return -1;
  }

  switch (type) {
    case MD_SPAN_EM:
    case MD_SPAN_STRONG:
    case MD_SPAN_A:
    case MD_SPAN_IMG:
    case MD_SPAN_CODE:
    case MD_SPAN_DEL:
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    case MD_SPAN_U:
      popNode();
      break;

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onText(MD_TEXTTYPE type, const char* text, MD_SIZE size) {
  MdNode* current = currentNode();
  if (!current) {
    return 0;
  }
  if (limitExceeded) {
    return -1;
  }

  std::string content(text, size);

  if (type == MD_TEXT_HTML && (!current || current->type != MdNodeType::HtmlBlock)) {
    if (content == "<mark>") {
      if (!pushNode(MdNode::createHighlight())) {
        return -1;
      }
      return 0;
    }
    if (content == "</mark>") {
      if (current && current->type == MdNodeType::Highlight) {
        popNode();
      }
      return 0;
    }
    if (content == "<sub>") {
      if (!pushNode(MdNode::createSubscript())) {
        return -1;
      }
      return 0;
    }
    if (content == "</sub>") {
      if (current && current->type == MdNodeType::Subscript) {
        popNode();
      }
      return 0;
    }
    if (content == "<sup>") {
      if (!pushNode(MdNode::createSuperscript())) {
        return -1;
      }
      return 0;
    }
    if (content == "</sup>") {
      if (current && current->type == MdNodeType::Superscript) {
        popNode();
      }
      return 0;
    }
  }

  switch (type) {
    case MD_TEXT_NORMAL:
      if (!appendTextNode(current, std::move(content))) {
        return -1;
      }
      break;

    case MD_TEXT_NULLCHAR:
      if (!appendTextNode(current, "\xEF\xBF\xBD")) {  // U+FFFD
        return -1;
      }
      break;

    case MD_TEXT_BR:
      if (!appendChildNode(current, MdNode::createHardBreak())) {
        return -1;
      }
      break;

    case MD_TEXT_SOFTBR:
      if (!appendChildNode(current, MdNode::createSoftBreak())) {
        return -1;
      }
      break;

    case MD_TEXT_ENTITY: {
      std::string decodedContent;
      if (content == "&amp;") {
        decodedContent = "&";
      } else if (content == "&lt;") {
        decodedContent = "<";
      } else if (content == "&gt;") {
        decodedContent = ">";
      } else if (content == "&quot;") {
        decodedContent = "\"";
      } else if (content == "&apos;") {
        decodedContent = "'";
      } else if (content == "&nbsp;") {
        decodedContent = " ";
      } else if (content.size() > 3 && content[1] == '#') {
        uint32_t codepoint = 0;
        if (parseNumericEntity(content, codepoint)) {
          decodedContent = encodeUtf8Codepoint(codepoint);
        } else {
          LOG_WRN("MD", "Malformed numeric entity: %s", content.c_str());
        }
      }

      if (decodedContent.empty()) {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      } else {
        if (!appendTextNode(current, std::move(decodedContent))) {
          return -1;
        }
      }
      break;
    }

    case MD_TEXT_CODE:
      // For inline code spans, append to the Code node's text
      if (current->type == MdNodeType::Code || current->type == MdNodeType::CodeBlock) {
        current->text += content;
      } else {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      }
      break;

    case MD_TEXT_HTML:
      // Raw HTML - append to HtmlBlock node
      if (current->type == MdNodeType::HtmlBlock) {
        current->text += content;
      } else {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      }
      break;

    case MD_TEXT_LATEXMATH:
      // LaTeX math content
      if (current->type == MdNodeType::LatexMath || current->type == MdNodeType::LatexMathDisplay) {
        current->text += content;
      } else {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      }
      break;

    default:
      if (!appendTextNode(current, std::move(content))) {
        return -1;
      }
      break;
  }

  return 0;
}

void MarkdownParser::setLimitExceeded(const char* reason) {
  if (limitExceeded) {
    return;
  }
  limitExceeded = true;
  LOG_ERR("MD", "Parse aborted: %s", reason);
}

bool MarkdownParser::checkNodeLimit() {
  if (nodeCount >= MAX_AST_NODES) {
    setLimitExceeded("AST node limit exceeded");
    return false;
  }
  return true;
}

bool MarkdownParser::checkDepthLimit(size_t nextDepth) {
  if (nextDepth > MAX_NESTING_DEPTH) {
    setLimitExceeded("AST nesting depth limit exceeded");
    return false;
  }
  return true;
}

bool MarkdownParser::appendChildNode(MdNode* parent, std::unique_ptr<MdNode> node) {
  if (!parent) {
    setLimitExceeded("Null parent while appending node");
    return false;
  }
  if (!checkNodeLimit()) {
    return false;
  }
  parent->appendChild(std::move(node));
  nodeCount++;
  return true;
}

bool MarkdownParser::appendTextNode(MdNode* parent, std::string text) {
  if (!parent) {
    setLimitExceeded("Null parent while appending text");
    return false;
  }
  if (text.empty()) {
    return true;
  }

  if (!parent->children.empty()) {
    MdNode* last = parent->children.back().get();
    if (last->type == MdNodeType::Text) {
      last->text += text;
      return true;
    }
  }

  return appendChildNode(parent, MdNode::createText(text));
}
