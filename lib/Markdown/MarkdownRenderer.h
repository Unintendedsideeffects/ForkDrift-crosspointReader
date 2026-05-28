#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "MarkdownAST.h"

class GfxRenderer;
class Page;
class PageImage;
class ParsedText;
class TextBlock;

// Renderer that converts MarkdownAST to Page objects
class MarkdownRenderer {
 public:
  static constexpr size_t MAX_RENDER_DEPTH = 50;
  static constexpr size_t MAX_NODE_MAPPING = 10000;

  // Callback for completed pages
  using PageCallback = std::function<void(std::unique_ptr<Page>)>;

  // Callback for progress updates (0-100)
  using ProgressCallback = std::function<void(int)>;

  MarkdownRenderer(GfxRenderer& renderer, int fontId, int viewportWidth, int viewportHeight, float lineCompression,
                   bool extraParagraphSpacing, uint8_t paragraphAlignment, bool hyphenationEnabled,
                   bool focusReadingEnabled, bool guideReadingEnabled, const std::string& contentBasePath);
  ~MarkdownRenderer();

  // Render AST to pages, calling pageCallback for each completed page
  // Returns true on success
  bool render(const MdNode& root, const PageCallback& pageCallback, const ProgressCallback& progressCallback = nullptr);

  // Get mapping of node indices to page numbers (call after render)
  const std::vector<size_t>& getNodeToPageMap() const { return nodeToPage; }

 private:
  GfxRenderer& renderer;
  int fontId;
  int viewportWidth;
  int viewportHeight;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool guideReadingEnabled;
  std::string contentBasePath;

  // Rendering state
  std::unique_ptr<Page> currentPage;
  std::unique_ptr<ParsedText> currentTextBlock;
  int currentPageNextY = 0;
  int pageCount = 0;
  size_t currentDepth = 0;
  bool depthLimitExceeded = false;

  // Style state
  bool isBold = false;
  bool isItalic = false;
  bool isPreformatted = false;
  int listDepth = 0;
  int blockquoteDepth = 0;

  // Node to page mapping
  std::vector<size_t> nodeToPage;
  size_t currentNodeIndex = 0;
  size_t totalNodes = 0;
  int lastProgress = -1;

  // Callbacks (set during render)
  PageCallback onPageComplete;
  ProgressCallback onProgress;

  // Rendering methods by node type
  void renderNode(const MdNode& node);
  void renderDocument(const MdNode& node);
  void renderHeading(const MdNode& node);
  void renderParagraph(const MdNode& node);
  void renderCodeBlock(const MdNode& node);
  void renderUnorderedList(const MdNode& node);
  void renderOrderedList(const MdNode& node);
  void renderListItem(const MdNode& node, bool ordered, int number);
  void renderBlockquote(const MdNode& node);
  void renderTable(const MdNode& node);
  void renderHorizontalRule(const MdNode& node);
  void renderHtmlBlock(const MdNode& node);

  // Inline rendering
  void renderInlineChildren(const MdNode& node);
  void renderText(const MdNode& node);
  void renderEmphasis(const MdNode& node);
  void renderStrong(const MdNode& node);
  void renderLink(const MdNode& node);
  void renderImage(const MdNode& node);
  void renderCode(const MdNode& node);
  void renderStrikethrough(const MdNode& node);
  void renderSoftBreak(const MdNode& node);
  void renderHardBreak(const MdNode& node);
  void renderWikiLink(const MdNode& node);
  void renderHighlight(const MdNode& node);
  void renderSubscript(const MdNode& node);
  void renderSuperscript(const MdNode& node);
  void renderLatexMath(const MdNode& node);

  // Page management
  void startNewTextBlock(uint8_t style);
  void flushTextBlock();
  void addLineToPage(std::shared_ptr<TextBlock> line);
  void addImageToPage(std::shared_ptr<PageImage> image);
  void finalizePage();

  // Helpers
  uint8_t getCurrentFontStyle() const;
  int getIndentWidth() const;
  std::string createIndentPrefix() const;
  void updateProgress();
};
