#include "MarkdownSection.h"

#include <Arduino.h>
#include <Logging.h>
#include <Serialization.h>

#include <cmath>
#include <vector>

#include "Epub/Page.h"
#include "MarkdownRenderer.h"
#include "SpiBusMutex.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 2;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) +
                                 sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t);
constexpr uint32_t MIN_SIZE_FOR_PROGRESS = 50 * 1024;
constexpr float LINE_COMPRESSION_EPSILON = 0.0001f;

bool nearlyEqual(const float a, const float b) { return std::fabs(a - b) <= LINE_COMPRESSION_EPSILON; }
}  // namespace

MarkdownSection::MarkdownSection(const std::string& cachePath, const std::string& contentBasePath,
                                 GfxRenderer& renderer)
    : cachePath(cachePath),
      contentBasePath(contentBasePath),
      renderer(renderer),
      filePath(cachePath + "/md_section.bin") {}

MarkdownSection::~MarkdownSection() { closeSectionFile(); }

void MarkdownSection::closeSectionFile() {
  if (file) {
    file.close();
  }
  fileOpenForReading = false;
}

uint32_t MarkdownSection::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("MSC", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("MSC", "Failed to serialize page %d", pageCount);
    return 0;
  }

  pageCount++;
  return position;
}

void MarkdownSection::writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing,
                                             bool forceParagraphIndents, uint8_t paragraphAlignment,
                                             uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                                             uint32_t sourceSize) {
  if (!file) {
    LOG_ERR("MSC", "File not open for writing header");
    return;
  }

  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(forceParagraphIndents) +
                                   sizeof(paragraphAlignment) + sizeof(viewportWidth) + sizeof(viewportHeight) +
                                   sizeof(hyphenationEnabled) + sizeof(sourceSize) + sizeof(pageCount) +
                                   sizeof(uint32_t),
                "Header size mismatch");

  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, forceParagraphIndents);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, sourceSize);
  serialization::writePod(file, pageCount);                 // Placeholder
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool MarkdownSection::loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing,
                                      bool forceParagraphIndents, uint8_t paragraphAlignment, uint16_t viewportWidth,
                                      uint16_t viewportHeight, bool hyphenationEnabled, uint32_t sourceSize) {
  SpiBusMutex::Guard guard;
  nodeToPageMap.clear();
  closeSectionFile();
  if (!Storage.openFileForRead("MSC", filePath, file)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPod(file, version)) {
    file.close();
    LOG_ERR("MSC", "Deserialization failed: truncated header");
    clearCache();
    return false;
  }
  if (version != SECTION_FILE_VERSION) {
    file.close();
    LOG_WRN("MSC", "Deserialization failed: unknown version %u", version);
    clearCache();
    return false;
  }

  int fileFontId;
  uint16_t fileViewportWidth, fileViewportHeight;
  float fileLineCompression;
  bool fileExtraParagraphSpacing;
  bool fileForceParagraphIndents;
  uint8_t fileParagraphAlignment;
  bool fileHyphenationEnabled;
  uint32_t fileSourceSize;

  if (!serialization::readPod(file, fileFontId) || !serialization::readPod(file, fileLineCompression) ||
      !serialization::readPod(file, fileExtraParagraphSpacing) ||
      !serialization::readPod(file, fileForceParagraphIndents) ||
      !serialization::readPod(file, fileParagraphAlignment) || !serialization::readPod(file, fileViewportWidth) ||
      !serialization::readPod(file, fileViewportHeight) || !serialization::readPod(file, fileHyphenationEnabled) ||
      !serialization::readPod(file, fileSourceSize)) {
    file.close();
    LOG_ERR("MSC", "Deserialization failed: truncated parameters");
    clearCache();
    return false;
  }

  if (fontId != fileFontId || !nearlyEqual(lineCompression, fileLineCompression) ||
      extraParagraphSpacing != fileExtraParagraphSpacing || forceParagraphIndents != fileForceParagraphIndents ||
      paragraphAlignment != fileParagraphAlignment || viewportWidth != fileViewportWidth ||
      viewportHeight != fileViewportHeight || hyphenationEnabled != fileHyphenationEnabled ||
      sourceSize != fileSourceSize) {
    file.close();
    LOG_WRN("MSC", "Deserialization failed: parameters do not match");
    clearCache();
    return false;
  }

  if (!serialization::readPod(file, pageCount)) {
    file.close();
    LOG_ERR("MSC", "Deserialization failed: truncated page count");
    clearCache();
    return false;
  }
  file.close();
  LOG_DBG("MSC", "Deserialization succeeded: %d pages", pageCount);
  return true;
}

bool MarkdownSection::clearCache() const {
  SpiBusMutex::Guard guard;
  const_cast<MarkdownSection*>(this)->closeSectionFile();
  if (!Storage.exists(filePath.c_str())) {
    return true;
  }
  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("MSC", "Failed to clear cache");
    return false;
  }
  return true;
}

bool MarkdownSection::createSectionFile(const MdNode& root, int fontId, float lineCompression,
                                        bool extraParagraphSpacing, bool forceParagraphIndents,
                                        uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                                        bool hyphenationEnabled, uint32_t sourceSize,
                                        const std::function<void()>& progressSetupFn,
                                        const std::function<void(int)>& progressFn) {
  SpiBusMutex::Guard guard;
  closeSectionFile();

  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }

  if (!Storage.openFileForWrite("MSC", filePath, file)) {
    return false;
  }

  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, forceParagraphIndents, paragraphAlignment,
                         viewportWidth, viewportHeight, hyphenationEnabled, sourceSize);

  if (progressSetupFn && sourceSize >= MIN_SIZE_FOR_PROGRESS) {
    progressSetupFn();
  }

  std::vector<uint32_t> lut = {};

  MarkdownRenderer mdRenderer(renderer, fontId, viewportWidth, viewportHeight, lineCompression, extraParagraphSpacing,
                              paragraphAlignment, hyphenationEnabled, contentBasePath);

  const bool success = mdRenderer.render(
      root, [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); },
      progressFn);

  if (!success) {
    LOG_ERR("MSC", "Failed to render markdown pages");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  nodeToPageMap = mdRenderer.getNodeToPageMap();

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("MSC", "Failed to write LUT due to invalid page positions");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  return true;
}

std::unique_ptr<Page> MarkdownSection::loadPageFromSectionFile() {
  SpiBusMutex::Guard guard;
  if (!fileOpenForReading) {
    if (!Storage.openFileForRead("MSC", filePath, file)) {
      return nullptr;
    }
    fileOpenForReading = true;
  }

  if (currentPage < 0 || static_cast<uint16_t>(currentPage) >= pageCount) {
    LOG_ERR("MSC", "Invalid page index %d (pageCount=%d)", currentPage, pageCount);
    closeSectionFile();
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pageOffset;
  serialization::readPod(file, pageOffset);
  file.seek(pageOffset);

  auto page = Page::deserialize(file);
  if (!page) {
    LOG_ERR("MSC", "Failed to deserialize page %d", currentPage);
    closeSectionFile();
    return nullptr;
  }
  return page;
}
