#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  // addWord's OOM guard sizes its heap check from wordgrowth:: (see
  // Epub/WordVectorGrowth.h) rather than a flat constant. The old flat 8KB is
  // gone: on top of heapguard's 32KB floor it refused to append a word while
  // ~41KB was still free, which blanked whole chapters on device.
  bool heapTruncated = false;  // this block hit the heap guard and was truncated

  std::vector<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<bool> wordContinues;      // true = word attaches to previous with no break
  std::vector<bool> wordNoSpaceBefore;  // true = may break before token, but no synthetic space when joined
  std::vector<bool> wordIsFocusSuffix;  // true = token is the regular tail of a focus bold-prefix split
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool forceParagraphIndents;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool guideReadingEnabled;
  bool isNaturalAlign;
  bool hasRtlWord;
  std::vector<std::string> reorderedWordsScratch;
  std::vector<EpdFontFamily::Style> reorderedStylesScratch;
  std::vector<uint16_t> reorderedWidthsScratch;
  std::vector<bool> reorderedContinuesScratch;
  std::vector<bool> reorderedNoSpaceBeforeScratch;
  std::vector<bool> reorderedFocusSuffixScratch;
  std::vector<uint16_t> visualOrderScratch;

  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                        std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                        std::vector<bool>& noSpaceBeforeVec);
  std::vector<size_t> computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  std::vector<bool>& noSpaceBeforeVec);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<bool>& continuesVec, const std::vector<bool>& noSpaceBeforeVec,
                   const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine, const GfxRenderer& renderer,
                   int fontId);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);
  void applyParagraphIndent();

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool forceParagraphIndents = false,
                      const bool hyphenationEnabled = false, const bool focusReadingEnabled = false,
                      const BlockStyle& blockStyle = BlockStyle(), const bool guideReadingEnabled = false)
      : blockStyle(blockStyle),
        extraParagraphSpacing(extraParagraphSpacing),
        forceParagraphIndents(forceParagraphIndents),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        guideReadingEnabled(guideReadingEnabled),
        isNaturalAlign(false),
        hasRtlWord(false) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false);

  // Build-scoped tally of how many blocks addWord's OOM guard truncated.
  //
  // Section::createSectionFile uses it to tell a chapter that laid out to nothing
  // because the heap was low (transient -- must not be cached, or the chapter is
  // blank forever) from one that is genuinely empty (fine to cache).
  //
  // A file-scope tally rather than a sink threaded through the constructor:
  // blocks are created, moved into table cells and moved back out again, so
  // there is no single finalization point to query, and a missed site would
  // silently under-report. Section building is strictly serialized -- one
  // section at a time, on the render task, under the storage mutex -- so there
  // is no concurrent builder to interleave with.
  static void resetHeapTruncationTally();
  static uint32_t heapTruncationTally();
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
