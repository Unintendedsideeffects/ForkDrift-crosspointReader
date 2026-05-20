#pragma once

#include <HalStorage.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "MarkdownAST.h"

class Page;
class GfxRenderer;

class MarkdownSection {
 public:
  MarkdownSection(const std::string& cachePath, const std::string& contentBasePath, GfxRenderer& renderer);
  ~MarkdownSection();

  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                       uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                       bool hyphenationEnabled, uint32_t sourceSize, bool guideReadingEnabled);
  bool createSectionFile(const MdNode& root, int fontId, float lineCompression, bool extraParagraphSpacing,
                         bool forceParagraphIndents, uint8_t paragraphAlignment, uint16_t viewportWidth,
                         uint16_t viewportHeight, bool hyphenationEnabled, uint32_t sourceSize,
                         bool guideReadingEnabled, const std::function<void()>& progressSetupFn = nullptr,
                         const std::function<void(int)>& progressFn = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();
  bool clearCache() const;

  const std::vector<size_t>& getNodeToPageMap() const { return nodeToPageMap; }
  bool hasNodeToPageMap() const { return !nodeToPageMap.empty(); }

  uint16_t pageCount = 0;
  int currentPage = 0;

 private:
  std::string cachePath;
  std::string contentBasePath;
  GfxRenderer& renderer;
  std::string filePath;
  HalFile file;
  bool fileOpenForReading = false;
  std::vector<size_t> nodeToPageMap;

  void writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                              uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                              bool hyphenationEnabled, uint32_t sourceSize, bool guideReadingEnabled);
  uint32_t onPageComplete(std::unique_ptr<Page> page);
  void closeSectionFile();
};
