#include "Section.h"

#include <FeatureFlags.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <Serialization.h>

#include <functional>

#include "Epub/css/CssParser.h"
#include "Page.h"
#if ENABLE_HYPHENATION
#include "hyphenation/Hyphenator.h"
#endif
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v30: SVG-wrapped <image> figures now parsed; bump forces re-index so books
// cached without those images regenerate with them.
constexpr uint8_t SECTION_FILE_VERSION = 30;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) +
                                 sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t);

struct PageLutEntry {
  uint32_t fileOffset;
  uint16_t paragraphIndex;
  uint16_t listItemIndex;
};
}  // namespace

void Section::closeSectionFile() {
  if (fileOpenForReading) {
    file.close();
    fileOpenForReading = false;
  }
}

uint32_t Section::onPageComplete(std::unique_ptr<Page> page, serialization::BufferedWriter& writer) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = writer.position();
  if (!page->serialize(writer)) {
    LOG_ERR("SCT", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(serialization::BufferedWriter& writer, const int fontId,
                                     const float lineCompression, const bool extraParagraphSpacing,
                                     const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                                     const uint16_t viewportWidth, const uint16_t viewportHeight,
                                     const bool hyphenationEnabled, const bool embeddedStyle,
                                     const uint8_t imageRendering, const bool focusReadingEnabled,
                                     const bool guideReadingEnabled) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(forceParagraphIndents) +
                                   sizeof(paragraphAlignment) + sizeof(viewportWidth) + sizeof(viewportHeight) +
                                   sizeof(pageCount) + sizeof(hyphenationEnabled) + sizeof(embeddedStyle) +
                                   sizeof(imageRendering) + sizeof(focusReadingEnabled) + sizeof(guideReadingEnabled) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(writer, SECTION_FILE_VERSION);
  serialization::writePod(writer, fontId);
  serialization::writePod(writer, lineCompression);
  serialization::writePod(writer, extraParagraphSpacing);
  serialization::writePod(writer, forceParagraphIndents);
  serialization::writePod(writer, paragraphAlignment);
  serialization::writePod(writer, viewportWidth);
  serialization::writePod(writer, viewportHeight);
  serialization::writePod(writer, hyphenationEnabled);
  serialization::writePod(writer, embeddedStyle);
  serialization::writePod(writer, imageRendering);
  serialization::writePod(writer, focusReadingEnabled);
  serialization::writePod(writer, guideReadingEnabled);
  serialization::writePod(writer, pageCount);  // Placeholder for page count (will be initially 0, patched later)
  serialization::writePod(writer, static_cast<uint32_t>(0));  // Placeholder for LUT offset (patched later)
  serialization::writePod(writer, static_cast<uint32_t>(0));  // Placeholder for anchor map offset (patched later)
  serialization::writePod(writer, static_cast<uint32_t>(0));  // Placeholder for paragraph LUT offset (patched later)
  serialization::writePod(writer, static_cast<uint32_t>(0));  // Placeholder for li LUT offset (patched later)
}

bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                              const uint16_t viewportWidth, const uint16_t viewportHeight,
                              const bool hyphenationEnabled, const bool embeddedStyle, const uint8_t imageRendering,
                              const bool focusReadingEnabled, const bool guideReadingEnabled) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      // Validation failed: close file before returning
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
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
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileFocusReadingEnabled;
    bool fileGuideReadingEnabled;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileForceParagraphIndents);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileImageRendering);
    serialization::readPod(file, fileFocusReadingEnabled);
    serialization::readPod(file, fileGuideReadingEnabled);

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || forceParagraphIndents != fileForceParagraphIndents ||
        paragraphAlignment != fileParagraphAlignment || viewportWidth != fileViewportWidth ||
        viewportHeight != fileViewportHeight || hyphenationEnabled != fileHyphenationEnabled ||
        embeddedStyle != fileEmbeddedStyle || imageRendering != fileImageRendering ||
        focusReadingEnabled != fileFocusReadingEnabled || guideReadingEnabled != fileGuideReadingEnabled) {
      // Validation failed: close file before returning
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  // File validation succeeded: keep it open for subsequent page loads
  fileOpenForReading = true;
  LOG_DBG("SCT", "Deserialization succeeded: %d pages", pageCount);
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() {
  // Close the section file before attempting to delete it
  closeSectionFile();

  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                const bool forceParagraphIndents, const uint8_t paragraphAlignment,
                                const uint16_t viewportWidth, const uint16_t viewportHeight,
                                const bool hyphenationEnabled, const bool embeddedStyle, const uint8_t imageRendering,
                                const bool focusReadingEnabled, const bool guideReadingEnabled, PopupCallback popupFn) {
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Retry logic for SD card timing issues
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
      delay(50);  // Brief delay before retry
    }

    // Remove any incomplete file from previous attempt before retrying
    if (Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
    }

    HalFile tmpHtml;
    if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    success = epub->readItemContentsToStream(localPath, tmpHtml, 1024);
    fileSize = tmpHtml.size();
    // Explicitly close() file before calling Storage.remove()
    tmpHtml.close();

    // If streaming failed, remove the incomplete file immediately
    if (!success && Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
      LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
    }
  }

  if (!success) {
    LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
    return false;
  }

  LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

  // Close any open read handle before opening for write
  closeSectionFile();

  if (!Storage.openFileForWrite("SCT", filePath, file)) {
    return false;
  }

  // Check heap before parsing: section indexing requires substantial memory
  if (!heapguard::canAllocate(0, 40 * 1024)) {
    LOG_ERR("SCT", "Insufficient heap for section indexing: %u bytes free",
            static_cast<unsigned>(heapguard::freeBytes()));
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // All section writes are sequential until the final header patch: batch them
  // through a sector-sized buffer so the storage mutex + SdFat are entered once
  // per 512 bytes instead of once per serialized field.
  serialization::BufferedWriter writer(file);
  writeSectionFileHeader(writer, fontId, lineCompression, extraParagraphSpacing, forceParagraphIndents,
                         paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled, embeddedStyle,
                         imageRendering, focusReadingEnabled, guideReadingEnabled);
  std::vector<PageLutEntry> lut = {};

  // Derive the content base directory and image cache path prefix for the parser
  size_t lastSlash = localPath.find_last_of('/');
  std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  std::string imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  CssParser* cssParser = nullptr;
  if (embeddedStyle) {
    cssParser = epub->getCssParser();
    if (cssParser) {
      if (!cssParser->loadFromCache()) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  ChapterHtmlSlimParser visitor(
      epub, tmpHtmlPath, renderer, fontId, lineCompression, extraParagraphSpacing, forceParagraphIndents,
      paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, guideReadingEnabled,
      [this, &lut, &writer](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex) {
        lut.push_back({this->onPageComplete(std::move(page), writer), paragraphIndex, listItemIndex});
      },
      embeddedStyle, contentBase, imageBasePath, imageRendering,
      popupFn.fn ? std::function<void()>([&popupFn]() { popupFn.fn(popupFn.ctx); }) : std::function<void()>(),
      cssParser);
#if ENABLE_HYPHENATION
  Hyphenator::setPreferredLanguage(epub->getLanguage());
#endif
  success = visitor.parseAndBuildPages();
  Storage.remove(tmpHtmlPath.c_str());
  if (!success) {
    LOG_ERR("SCT", "Failed to parse XML and build pages");
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove(filePath.c_str());
    if (cssParser) {
      cssParser->clear();
    }
    return false;
  }

  const uint32_t lutOffset = writer.position();
  bool hasFailedLutRecords = false;
  // Write LUT
  for (const auto& entry : lut) {
    if (entry.fileOffset == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(writer, entry.fileOffset);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets).
  // Clamp the count to what the uint16_t header can express AND only write that many:
  // writing all entries under a truncated count would desync the file on readback
  // (the reader would parse trailing anchor bytes as the next section). A malicious
  // chapter with >65535 id attributes is the trigger.
  const uint32_t anchorMapOffset = writer.position();
  const auto& anchors = visitor.getAnchors();
  const size_t anchorCount = anchors.size() > UINT16_MAX ? UINT16_MAX : anchors.size();
  serialization::writePod(writer, static_cast<uint16_t>(anchorCount));
  for (size_t i = 0; i < anchorCount; i++) {
    serialization::writeString(writer, anchors[i].first);
    serialization::writePod(writer, anchors[i].second);
  }

  // Same clamp rationale for the paragraph LUT count.
  const uint32_t paragraphLutOffset = writer.position();
  const size_t lutCount = lut.size() > UINT16_MAX ? UINT16_MAX : lut.size();
  serialization::writePod(writer, static_cast<uint16_t>(lutCount));
  for (size_t i = 0; i < lutCount; i++) {
    serialization::writePod(writer, lut[i].paragraphIndex);
  }

  const uint32_t liLutFileOffset = static_cast<uint32_t>(writer.position());
  for (const auto& entry : lut) {
    serialization::writePod(writer, entry.listItemIndex);
  }

  // Drain the write buffer before seeking back to patch the header; a failed
  // flush means the file is incomplete and must be discarded like any other
  // serialization failure.
  if (!writer.flush()) {
    LOG_ERR("SCT", "Failed to flush section file writes");
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove(filePath.c_str());
    if (cssParser) {
      cssParser->clear();
    }
    return false;
  }

  // Patch header with final pageCount, lutOffset, anchorMapOffset, paragraphLutOffset, and liLutOffset
  file.seek(HEADER_SIZE - sizeof(uint32_t) * 4 - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  serialization::writePod(file, anchorMapOffset);
  serialization::writePod(file, paragraphLutOffset);
  serialization::writePod(file, liLutFileOffset);
  // Explicit close() required: member variable persists beyond function scope
  file.close();
  if (cssParser) {
    cssParser->clear();
  }
  return true;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  // Guard internally too: a negative currentPage would make static_cast<uint32_t>
  // below wrap to a huge offset that can slip past the lutEntryOffset bounds check
  // and seek to an arbitrary position. Callers guard, but don't rely on that here.
  if (currentPage < 0) {
    LOG_ERR("SECTION", "negative currentPage=%d", currentPage);
    return nullptr;
  }

  // Check if file is already open from loadSectionFile(). If not, open it.
  bool weOpenedFile = false;
  if (!fileOpenForReading) {
    if (!Storage.openFileForRead("SCT", filePath, file)) {
      return nullptr;
    }
    weOpenedFile = true;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t) * 4);
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  if (lutOffset == 0 || lutOffset >= file.size()) {
    LOG_ERR("SECTION", "invalid lutOffset=%u in section file", (unsigned)lutOffset);
    if (weOpenedFile) {
      file.close();
    }
    return nullptr;
  }
  const uint32_t lutEntryOffset = lutOffset + sizeof(uint32_t) * static_cast<uint32_t>(currentPage);
  if (lutEntryOffset + sizeof(uint32_t) > file.size()) {
    LOG_ERR("SECTION", "LUT entry for page %d out of bounds", currentPage);
    if (weOpenedFile) {
      file.close();
    }
    return nullptr;
  }
  file.seek(lutEntryOffset);
  uint32_t pagePos;
  if (!serialization::readPod(file, pagePos) || pagePos == 0 || pagePos >= file.size()) {
    LOG_ERR("SECTION", "invalid pagePos=%u for page %d", (unsigned)pagePos, currentPage);
    if (weOpenedFile) {
      file.close();
    }
    return nullptr;
  }
  file.seek(pagePos);

  // Page data is read strictly sequentially from pagePos: batch the ~1000 tiny
  // field reads through a sector-sized buffer (one mutex/SdFat call per 512B).
  serialization::BufferedReader reader(file);
  auto page = Page::deserialize(reader);
  // Only close if we opened it in this call; keep persistent handle if opened from loadSectionFile()
  if (weOpenedFile) {
    file.close();
  }
  return page;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  f.seek(anchorMapOffset);
  serialization::BufferedReader reader(f);
  uint16_t count;
  serialization::readPod(reader, count);
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    serialization::readString(reader, key);
    serialization::readPod(reader, page);
    if (key == anchor) {
      f.close();
      return page;
    }
  }

  f.close();
  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    f.close();
    return std::nullopt;
  }

  const uint64_t lutEnd =
      static_cast<uint64_t>(paragraphLutOffset) + sizeof(uint16_t) + static_cast<uint64_t>(count) * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    f.close();
    return std::nullopt;
  }

  serialization::BufferedReader reader(f);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pagePIdx;
    serialization::readPod(reader, pagePIdx);
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  f.close();
  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    f.close();
    return std::nullopt;
  }

  const uint64_t entryEnd =
      static_cast<uint64_t>(paragraphLutOffset) + sizeof(uint16_t) + static_cast<uint64_t>(page + 1) * sizeof(uint16_t);
  if (entryEnd > fileSize) {
    f.close();
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t));
  uint16_t pIdx;
  serialization::readPod(f, pIdx);
  f.close();
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t liLutOffset;
  serialization::readPod(f, liLutOffset);
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint64_t lutEnd = static_cast<uint64_t>(liLutOffset) + static_cast<uint64_t>(count) * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(liLutOffset);
  serialization::BufferedReader reader(f);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx;
    serialization::readPod(reader, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}
