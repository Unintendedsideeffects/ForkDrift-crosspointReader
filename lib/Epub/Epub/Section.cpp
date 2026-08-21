#include "Section.h"

#include <FeatureFlags.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <Serialization.h>
#include <freertos/task.h>

#include <functional>

#include "Epub/ParsedText.h"
#include "Epub/css/CssParser.h"
#include "Page.h"
#if ENABLE_HYPHENATION
#include "hyphenation/Hyphenator.h"
#endif
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v33: the user's paragraph alignment again overrides embedded text-align
// (v32: RTL/bidi line layout; v31: SVG images + NFC composition); alignment is
// baked into the laid-out lines, so cached sections keep the old alignment
// until this invalidates them.
constexpr uint8_t SECTION_FILE_VERSION = 33;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) +
                                 sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t);

struct PageLutEntry {
  uint32_t fileOffset;
  uint16_t paragraphIndex;
  uint16_t listItemIndex;
};

constexpr size_t kSectionBuildYieldInterval = 8;  // Prevents the reader from freezing during long cache builds.

void yieldDuringSectionBuild(const size_t completedItems) {
  if (completedItems % kSectionBuildYieldInterval == 0) {
    vTaskDelay(1);
  }
}

bool rangeWithinFile(const uint32_t offset, const uint64_t bytes, const uint32_t fileSize) {
  return offset <= fileSize && bytes <= static_cast<uint64_t>(fileSize - offset);
}

bool validateSectionFileStructure(HalFile& file, const uint32_t fileSize, const uint16_t pageCount,
                                  const uint32_t lutOffset, const uint32_t anchorMapOffset,
                                  const uint32_t paragraphLutOffset, const uint32_t liLutOffset) {
  const uint64_t pageLutBytes = static_cast<uint64_t>(pageCount) * sizeof(uint32_t);
  if (lutOffset < HEADER_SIZE || !rangeWithinFile(lutOffset, pageLutBytes, fileSize) ||
      static_cast<uint64_t>(lutOffset) + pageLutBytes != anchorMapOffset || anchorMapOffset > paragraphLutOffset ||
      paragraphLutOffset > liLutOffset || liLutOffset > fileSize) {
    return false;
  }

  if (!file.seek(lutOffset)) return false;
  uint32_t previousPageOffset = 0;
  for (uint16_t i = 0; i < pageCount; i++) {
    uint32_t pageOffset = 0;
    if (!serialization::readPod(file, pageOffset) || pageOffset < HEADER_SIZE || pageOffset >= lutOffset ||
        (i > 0 && pageOffset <= previousPageOffset)) {
      return false;
    }
    previousPageOffset = pageOffset;
  }

  if (!rangeWithinFile(anchorMapOffset, sizeof(uint16_t), fileSize) || !file.seek(anchorMapOffset)) return false;
  uint16_t anchorCount = 0;
  if (!serialization::readPod(file, anchorCount)) return false;
  for (uint16_t i = 0; i < anchorCount; i++) {
    uint32_t length = 0;
    if (!serialization::readPod(file, length) || length > 65536 ||
        !rangeWithinFile(static_cast<uint32_t>(file.position()), static_cast<uint64_t>(length) + sizeof(uint16_t),
                         paragraphLutOffset) ||
        !file.seek(file.position() + length)) {
      return false;
    }
    uint16_t anchorPage = 0;
    if (!serialization::readPod(file, anchorPage) || anchorPage >= pageCount) return false;
  }
  if (file.position() != paragraphLutOffset || !rangeWithinFile(paragraphLutOffset, sizeof(uint16_t), fileSize) ||
      !file.seek(paragraphLutOffset)) {
    return false;
  }

  uint16_t paragraphCount = 0;
  if (!serialization::readPod(file, paragraphCount) || paragraphCount != pageCount ||
      static_cast<uint64_t>(paragraphLutOffset) + sizeof(uint16_t) +
              static_cast<uint64_t>(paragraphCount) * sizeof(uint16_t) !=
          liLutOffset ||
      !rangeWithinFile(liLutOffset, static_cast<uint64_t>(paragraphCount) * sizeof(uint16_t), fileSize)) {
    return false;
  }
  return true;
}
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
  yieldDuringSectionBuild(pageCount);
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
  closeSectionFile();
  pageCount = 0;
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  const auto rejectCache = [this](const char* reason) {
    file.close();
    fileOpenForReading = false;
    pageCount = 0;
    LOG_ERR("SCT", "Deserialization failed: %s", reason);
    clearCache();
    return false;
  };

  const uint32_t fileSize = file.size();
  if (fileSize < HEADER_SIZE) {
    return rejectCache("truncated section header");
  }

  // Match parameters
  {
    uint8_t version = 0;
    if (!serialization::readPod(file, version)) {
      return rejectCache("could not read version");
    }
    if (version != SECTION_FILE_VERSION) {
      LOG_ERR("SCT", "Unknown section cache version %u", version);
      return rejectCache("unknown version");
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
    if (!serialization::readPod(file, fileFontId) || !serialization::readPod(file, fileLineCompression) ||
        !serialization::readPod(file, fileExtraParagraphSpacing) ||
        !serialization::readPod(file, fileForceParagraphIndents) ||
        !serialization::readPod(file, fileParagraphAlignment) || !serialization::readPod(file, fileViewportWidth) ||
        !serialization::readPod(file, fileViewportHeight) || !serialization::readPod(file, fileHyphenationEnabled) ||
        !serialization::readPod(file, fileEmbeddedStyle) || !serialization::readPod(file, fileImageRendering) ||
        !serialization::readPod(file, fileFocusReadingEnabled) ||
        !serialization::readPod(file, fileGuideReadingEnabled)) {
      return rejectCache("truncated section parameters");
    }

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || forceParagraphIndents != fileForceParagraphIndents ||
        paragraphAlignment != fileParagraphAlignment || viewportWidth != fileViewportWidth ||
        viewportHeight != fileViewportHeight || hyphenationEnabled != fileHyphenationEnabled ||
        embeddedStyle != fileEmbeddedStyle || imageRendering != fileImageRendering ||
        focusReadingEnabled != fileFocusReadingEnabled || guideReadingEnabled != fileGuideReadingEnabled) {
      return rejectCache("parameters do not match");
    }
  }

  uint32_t lutOffset = 0;
  uint32_t anchorMapOffset = 0;
  uint32_t paragraphLutOffset = 0;
  uint32_t liLutOffset = 0;
  if (!serialization::readPod(file, pageCount) || !serialization::readPod(file, lutOffset) ||
      !serialization::readPod(file, anchorMapOffset) || !serialization::readPod(file, paragraphLutOffset) ||
      !serialization::readPod(file, liLutOffset)) {
    return rejectCache("truncated section offsets");
  }
  if (!validateSectionFileStructure(file, fileSize, pageCount, lutOffset, anchorMapOffset, paragraphLutOffset,
                                    liLutOffset)) {
    return rejectCache("invalid section cache structure");
  }
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
  // Counted so a chapter that laid out to nothing can be told apart from one
  // that laid out normally; see the refusal below parseAndBuildPages().
  size_t totalPageElements = 0;
  ParsedText::resetHeapTruncationTally();

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

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  ChapterHtmlSlimParser visitor(
      epub, tmpHtmlPath, renderer, fontId, lineCompression, extraParagraphSpacing, forceParagraphIndents,
      paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, guideReadingEnabled,
      [this, &lut, &writer, &totalPageElements](std::unique_ptr<Page> page, const uint16_t paragraphIndex,
                                                const uint16_t listItemIndex) {
        totalPageElements += page ? page->elements.size() : 0;
        lut.push_back({this->onPageComplete(std::move(page), writer), paragraphIndex, listItemIndex});
      },
      embeddedStyle, contentBase, imageBasePath, imageRendering, std::move(tocAnchors),
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

  // Refuse to cache a chapter that laid out to nothing *because the heap was
  // low*. ParsedText::addWord discards every word while free heap is under
  // ~41KB (canAllocate's total-free clause, not fragmentation), so an
  // image-heavy chapter can produce pages with no elements at all. Writing that
  // is far worse than failing: the blank section is cached on SD and every
  // subsequent open reads the blank cache instead of re-indexing, so a
  // transient dip in free heap blanks the chapter permanently. Same sticky
  // shape as the empty-CSS-parse defect (docs/FINDINGS.md 2026-08-12T19:45Z).
  //
  // Both conditions are required. Zero elements alone is a legitimate result —
  // EPUBs do contain genuinely empty spine items, and refusing those would make
  // them re-index (and error) on every single open. The truncation tally is what
  // says the emptiness was caused by heap pressure rather than by the content.
  //
  // Deliberately NOT extended to partially truncated sections: those are lossy
  // and are still cached. Refusing them would turn a partly readable chapter
  // into an unreadable one on a device that cannot currently index it at all,
  // which is a worse trade than it looks. Recorded as a known gap.
  if (totalPageElements == 0 && ParsedText::heapTruncationTally() > 0) {
    LOG_ERR("SCT", "Refusing to cache empty section: %u block(s) dropped by the heap guard (free=%u largest=%u)",
            static_cast<unsigned>(ParsedText::heapTruncationTally()), static_cast<unsigned>(heapguard::freeBytes()),
            static_cast<unsigned>(heapguard::largestBlock()));
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
  size_t lutRecordsWritten = 0;
  // Write LUT
  for (const auto& entry : lut) {
    if (entry.fileOffset == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(writer, entry.fileOffset);
    yieldDuringSectionBuild(++lutRecordsWritten);
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
    yieldDuringSectionBuild(i + 1);
  }

  // Same clamp rationale for the paragraph LUT count.
  const uint32_t paragraphLutOffset = writer.position();
  const size_t lutCount = lut.size() > UINT16_MAX ? UINT16_MAX : lut.size();
  serialization::writePod(writer, static_cast<uint16_t>(lutCount));
  for (size_t i = 0; i < lutCount; i++) {
    serialization::writePod(writer, lut[i].paragraphIndex);
    yieldDuringSectionBuild(i + 1);
  }

  const uint32_t liLutFileOffset = static_cast<uint32_t>(writer.position());
  size_t liLutRecordsWritten = 0;
  for (const auto& entry : lut) {
    serialization::writePod(writer, entry.listItemIndex);
    yieldDuringSectionBuild(++liLutRecordsWritten);
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

  if (currentPage >= pageCount || !file.seek(HEADER_SIZE - sizeof(uint32_t) * 4)) {
    LOG_ERR("SECTION", "page %d is outside cached range %u", currentPage, pageCount);
    if (weOpenedFile) file.close();
    return nullptr;
  }
  uint32_t lutOffset = 0;
  if (!serialization::readPod(file, lutOffset)) {
    if (weOpenedFile) file.close();
    return nullptr;
  }
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
  if (!file.seek(lutEntryOffset)) {
    if (weOpenedFile) file.close();
    return nullptr;
  }
  uint32_t pagePos = 0;
  if (!serialization::readPod(file, pagePos) || pagePos < HEADER_SIZE || pagePos >= lutOffset) {
    LOG_ERR("SECTION", "invalid pagePos=%u for page %d", (unsigned)pagePos, currentPage);
    if (weOpenedFile) {
      file.close();
    }
    return nullptr;
  }
  if (!file.seek(pagePos)) {
    if (weOpenedFile) file.close();
    return nullptr;
  }

  // Page data is read strictly sequentially from pagePos: batch the ~1000 tiny
  // field reads through a sector-sized buffer (one mutex/SdFat call per 512B).
  serialization::BufferedReader reader(file);
  auto page = Page::deserialize(reader);
  // Only close if we opened it in this call; keep persistent handle if opened from loadSectionFile()
  if (weOpenedFile) {
    file.close();
  }
  if (!page) {
    LOG_ERR("SECTION", "page %d cache payload is corrupt; clearing cache", currentPage);
    pageCount = 0;
    clearCache();
  }
  return page;
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = this->loadPageFromSectionFile();
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& words = line.getBlock()->getWords();
          for (const auto& w : words) {
            if (!fullText.empty()) fullText += " ";
            fullText += w;
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (fileSize < HEADER_SIZE) {
    return std::nullopt;
  }

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 4 - sizeof(uint16_t))) return std::nullopt;
  uint16_t count = 0;
  if (!serialization::readPod(f, count)) return std::nullopt;
  return count;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 3)) return std::nullopt;
  uint32_t anchorMapOffset = 0;
  if (!serialization::readPod(f, anchorMapOffset)) return std::nullopt;
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  if (!f.seek(anchorMapOffset)) return std::nullopt;
  serialization::BufferedReader reader(f);
  uint16_t count = 0;
  if (!serialization::readPod(reader, count)) return std::nullopt;
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page = 0;
    if (!serialization::readString(reader, key) || !serialization::readPod(reader, page)) return std::nullopt;
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
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 2)) return std::nullopt;
  uint32_t paragraphLutOffset = 0;
  if (!serialization::readPod(f, paragraphLutOffset)) return std::nullopt;
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  if (!f.seek(paragraphLutOffset)) return std::nullopt;
  uint16_t count = 0;
  if (!serialization::readPod(f, count)) return std::nullopt;
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
    uint16_t pagePIdx = 0;
    if (!serialization::readPod(reader, pagePIdx)) return std::nullopt;
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
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 2)) return std::nullopt;
  uint32_t paragraphLutOffset = 0;
  if (!serialization::readPod(f, paragraphLutOffset)) return std::nullopt;
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  if (!f.seek(paragraphLutOffset)) return std::nullopt;
  uint16_t count = 0;
  if (!serialization::readPod(f, count)) return std::nullopt;
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

  if (!f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t))) return std::nullopt;
  uint16_t pIdx = 0;
  if (!serialization::readPod(f, pIdx)) return std::nullopt;
  f.close();
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t))) return std::nullopt;
  uint32_t liLutOffset = 0;
  if (!serialization::readPod(f, liLutOffset)) return std::nullopt;
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 2)) return std::nullopt;
  uint32_t paragraphLutOffset = 0;
  if (!serialization::readPod(f, paragraphLutOffset)) return std::nullopt;
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  if (!f.seek(paragraphLutOffset)) return std::nullopt;
  uint16_t count = 0;
  if (!serialization::readPod(f, count)) return std::nullopt;
  if (count == 0) {
    return std::nullopt;
  }

  const uint64_t lutEnd = static_cast<uint64_t>(liLutOffset) + static_cast<uint64_t>(count) * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  if (!f.seek(liLutOffset)) return std::nullopt;
  serialization::BufferedReader reader(f);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx = 0;
    if (!serialization::readPod(reader, pageLiIdx)) return std::nullopt;
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}
