#include "SdCardFont.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>

#include "EpdFontFamily.h"

static_assert(sizeof(EpdGlyph) == 16, "EpdGlyph must be 16 bytes to match .cpfont file layout");
static_assert(sizeof(EpdUnicodeInterval) == 12, "EpdUnicodeInterval must be 12 bytes to match .cpfont file layout");
static_assert(sizeof(EpdKernClassEntry) == 3, "EpdKernClassEntry must be 3 bytes to match .cpfont file layout");
static_assert(sizeof(EpdLigaturePair) == 8, "EpdLigaturePair must be 8 bytes to match .cpfont file layout");

namespace {

// FNV-1a hash for content-based font ID generation
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t hash = FNV_OFFSET) {
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

// .cpfont magic bytes
constexpr char CPFONT_MAGIC[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
// CPFONT_VERSION is defined as a #define in SdCardFont.h so it can be
// stringified into FONT_MANIFEST_URL.
constexpr uint32_t HEADER_SIZE = 32;
constexpr uint32_t STYLE_TOC_ENTRY_SIZE = 32;

// Helper to read little-endian values from byte buffer
inline uint16_t readU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline int16_t readI16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }
inline uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

class LowestCodepoints {
 public:
  LowestCodepoints(uint32_t* buffer, const uint32_t limit) : codepoints_(buffer), limit_(limit) {}

  void add(const uint32_t cp) {
    if (count_ == limit_ && cp >= codepoints_[largestAt_]) return;
    if (contains(cp)) return;
    if (count_ < limit_) {
      codepoints_[count_++] = cp;
      if (count_ == limit_) findLargest();
      return;
    }
    codepoints_[largestAt_] = cp;
    findLargest();
  }

  void addReserved(const uint32_t cp) {
    if (!contains(cp)) codepoints_[count_++] = cp;
  }

  uint32_t* data() const { return codepoints_; }
  uint32_t size() const { return count_; }

 private:
  bool contains(const uint32_t cp) const {
    return std::find(codepoints_, codepoints_ + count_, cp) != codepoints_ + count_;
  }
  void findLargest() {
    largestAt_ = static_cast<uint32_t>(std::max_element(codepoints_, codepoints_ + limit_) - codepoints_);
  }

  uint32_t* codepoints_;
  uint32_t limit_;
  uint32_t count_ = 0;
  uint32_t largestAt_ = 0;
};

template <typename Wanted>
void collectCodepoints(const char* text, LowestCodepoints& collected, Wanted wanted) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (wanted(cp)) collected.add(cp);
  }
}

const char* asCStr(const std::string& s) { return s.c_str(); }
const char* asCStr(const char* s) { return s; }

struct SharedIntervals {
  const void* table = nullptr;
  uint32_t count = 0;
  bool bmp16 = false;
  uint16_t refs = 0;
};
constexpr size_t MAX_SHARED_INTERVALS = 16;
SharedIntervals sharedIntervals[MAX_SHARED_INTERVALS];

}  // namespace

SdCardFont::~SdCardFont() { freeAll(); }

// --- Per-style free/cleanup ---

void SdCardFont::freeStyleMiniData(PerStyle& s) {
  delete[] s.miniIntervals;
  s.miniIntervals = nullptr;
  delete[] s.miniGlyphs;
  s.miniGlyphs = nullptr;
  delete[] s.miniBitmap;
  s.miniBitmap = nullptr;
  s.miniIntervalCount = 0;
  s.miniGlyphCount = 0;
  freeStyleMiniKern(s);
  memset(&s.miniData, 0, sizeof(s.miniData));
  s.epdFont.data = &s.stubData;
}

void SdCardFont::freeStyleLigatures(PerStyle& s) {
  s.stubData.ligaturePairs = nullptr;
  s.stubData.ligaturePairCount = 0;
  s.miniData.ligaturePairs = nullptr;
  s.miniData.ligaturePairCount = 0;
  delete[] s.ligaturePairs;
  s.ligaturePairs = nullptr;
  s.ligaturesLoaded = false;
}

void SdCardFont::freeStyleMiniKern(PerStyle& s) {
  delete[] s.miniKernLeftClasses;
  s.miniKernLeftClasses = nullptr;
  delete[] s.miniKernRightClasses;
  s.miniKernRightClasses = nullptr;
  delete[] s.miniKernMatrix;
  s.miniKernMatrix = nullptr;
  s.miniKernLeftEntryCount = 0;
  s.miniKernRightEntryCount = 0;
  s.miniKernLeftClassCount = 0;
  s.miniKernRightClassCount = 0;
}

void SdCardFont::releaseIntervals(PerStyle& s) {
  const void* table = s.intervalsAreBmp16 ? static_cast<const void*>(s.bmpIntervals) : s.fullIntervals;
  bool lastReference = true;
  for (auto& entry : sharedIntervals) {
    if (!table || entry.table != table) continue;
    lastReference = --entry.refs == 0;
    if (lastReference) entry = SharedIntervals{};
    break;
  }
  if (lastReference) {
    delete[] s.fullIntervals;
    delete[] s.bmpIntervals;
  }
  s.fullIntervals = nullptr;
  s.bmpIntervals = nullptr;
}

void SdCardFont::freeStyleAll(PerStyle& s) {
  freeStyleMiniData(s);
  releaseIntervals(s);
  s.intervalsAreBmp16 = false;
  freeStyleLigatures(s);
  delete[] s.kernBlockIndex;
  s.kernBlockIndex = nullptr;
  s.kernBlockIndexReady = false;
}

void SdCardFont::freeAll() {
  clearOverflow();
  clearPersistentCache();
  for (uint8_t i = 0; i < styleCount_; i++) {
    freeStyleAll(styles_[i]);
  }
  styles_.reset();
  memset(styleSlots_, 0, sizeof(styleSlots_));
  styleCount_ = 0;
  contentHash_ = 0;
  loaded_ = false;
}

void SdCardFont::clearOverflow() {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    delete[] overflow_[i].bitmap;
    overflow_[i].bitmap = nullptr;
    overflow_[i].codepoint = 0;
  }
  overflowCount_ = 0;
  overflowNext_ = 0;
}

// --- Per-style kern/ligature ---

void SdCardFont::applyKernLigaturePointers(PerStyle& s, EpdFontData& data) const {
  // Kern data uses the per-page mini tables (renumbered class IDs). The full
  // kern matrix is never resident — see PerStyle::miniKernMatrix comment.
  data.kernLeftClasses = s.miniKernLeftClasses;
  data.kernRightClasses = s.miniKernRightClasses;
  data.kernLeftCodepoints = nullptr;
  data.kernLeftClassIds = nullptr;
  data.kernRightCodepoints = nullptr;
  data.kernRightClassIds = nullptr;
  data.kernRowOffsets = nullptr;
  data.kernSparseCols = nullptr;
  data.kernSparseValues = nullptr;
  data.kernMatrix = s.miniKernMatrix;
  data.kernLeftEntryCount = s.miniKernLeftEntryCount;
  data.kernRightEntryCount = s.miniKernRightEntryCount;
  data.kernLeftClassCount = s.miniKernLeftClassCount;
  data.kernRightClassCount = s.miniKernRightClassCount;
  // Ligatures are small (typically < 1KB) so they stay resident.
  data.ligaturePairs = s.ligaturePairs;
  data.ligaturePairCount = s.header.ligaturePairCount;
}

bool SdCardFont::loadStyleLigatures(PerStyle& s) {
  if (s.ligaturesLoaded) return true;
  if (s.header.ligaturePairCount == 0) {
    s.ligaturesLoaded = true;
    return true;
  }

  HalFile file;
  if (!Storage.openFileForRead("SDCF", filePath_, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont for ligatures: %s", filePath_);
    return false;
  }
  s.ligaturePairs = new (std::nothrow) EpdLigaturePair[s.header.ligaturePairCount];
  if (!s.ligaturePairs) {
    LOG_ERR("SDCF", "Failed to allocate ligature pairs");
    return false;
  }
  if (!file.seekSet(s.ligatureFileOffset)) {
    LOG_ERR("SDCF", "Failed to seek to ligature data");
    freeStyleLigatures(s);
    return false;
  }
  size_t sz = s.header.ligaturePairCount * sizeof(EpdLigaturePair);
  if (file.read(reinterpret_cast<uint8_t*>(s.ligaturePairs), sz) != static_cast<int>(sz)) {
    LOG_ERR("SDCF", "Failed to read ligature pairs");
    freeStyleLigatures(s);
    return false;
  }

  s.ligaturesLoaded = true;
  s.stubData.ligaturePairs = s.ligaturePairs;
  s.stubData.ligaturePairCount = s.header.ligaturePairCount;

  LOG_DBG("SDCF", "Ligatures loaded: %u", s.header.ligaturePairCount);
  return true;
}

namespace {
constexpr uint16_t KERN_CLASS_BLOCK = 64;

uint16_t kernClassBlocks(const uint16_t entryCount) { return (entryCount + KERN_CLASS_BLOCK - 1) / KERN_CLASS_BLOCK; }

bool readKernClasses(HalFile& file, const uint32_t offset, const uint16_t entryCount, const uint8_t classCount,
                     const uint32_t* codepoints, const uint32_t cpCount, uint8_t* classes, EpdKernClassEntry* block,
                     uint16_t* index, const bool indexReady) {
  memset(classes, 0, cpCount);
  const uint16_t blocks = kernClassBlocks(entryCount);
  uint32_t next = 0;
  for (uint16_t b = 0; b < blocks && (next < cpCount || (index && !indexReady)); b++) {
    if (index && indexReady) {
      while (next < cpCount && codepoints[next] < index[b]) next++;
      const uint32_t end = b + 1 < blocks ? index[b + 1] : index[blocks] + 1u;
      if (next == cpCount || codepoints[next] >= static_cast<uint32_t>(index[blocks]) + 1u) break;
      if (codepoints[next] >= end) continue;
    }
    const uint16_t count = std::min<uint16_t>(KERN_CLASS_BLOCK, entryCount - b * KERN_CLASS_BLOCK);
    const int bytes = count * sizeof(EpdKernClassEntry);
    if (!file.seekSet(offset + static_cast<uint32_t>(b) * KERN_CLASS_BLOCK * sizeof(EpdKernClassEntry)) ||
        file.read(reinterpret_cast<uint8_t*>(block), bytes) != bytes) {
      return false;
    }
    if (index && !indexReady) {
      index[b] = block[0].codepoint;
      if (b + 1 == blocks) index[blocks] = block[count - 1].codepoint;
    }
    for (uint16_t i = 0; i < count && next < cpCount; i++) {
      const uint32_t cp = block[i].codepoint;
      const uint8_t classId = block[i].classId <= classCount ? block[i].classId : 0;
      while (next < cpCount && codepoints[next] < cp) next++;
      for (uint32_t j = next; j < cpCount && codepoints[j] == cp; j++) classes[j] = classId;
    }
  }
  return true;
}
}  // namespace

// --- Per-page mini kern matrix ---

// Build a small per-page kern matrix containing ONLY the (leftClass, rightClass)
// pairs reachable from codepoints in the current text. Class IDs are renumbered
// to a dense 1..N range so the resulting matrix is usedLeft × usedRight (typical
// Latin page: ~25×25 bytes) instead of the font's full ~180×200 (~36KB).
//
// Correctness: getKerning takes split/CSR when those pointers are non-null,
// else packed+dense. applyKernLigaturePointers nulls split/CSR and swaps the
// packed+dense fields to the mini tables together, so a codepoint not on this
// page returns class 0 (no kerning).
bool SdCardFont::buildMiniKernMatrix(PerStyle& s, const uint32_t* codepoints, uint32_t cpCount) {
  freeStyleMiniKern(s);
  if (s.header.kernLeftEntryCount == 0 || s.header.kernRightEntryCount == 0 || cpCount == 0) {
    return true;
  }

  HalFile file;
  if (!Storage.openFileForRead("SDCF", filePath_, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont for mini kern: %s", filePath_);
    return false;
  }

  const uint32_t ioBytes =
      std::max<uint32_t>(KERN_CLASS_BLOCK * sizeof(EpdKernClassEntry), s.header.kernRightClassCount);
  auto scratch = makeUniqueNoThrow<uint8_t[]>(cpCount * 2 + ioBytes);
  if (!scratch) {
    LOG_ERR("SDCF", "Failed to allocate mini kern scratch (%u bytes)", cpCount * 2 + ioBytes);
    return false;
  }
  uint8_t* leftClasses = scratch.get();
  uint8_t* rightClasses = leftClasses + cpCount;
  uint8_t* io = rightClasses + cpCount;
  auto* block = reinterpret_cast<EpdKernClassEntry*>(io);
  uint16_t* rightIndex =
      s.kernBlockIndex ? s.kernBlockIndex + kernClassBlocks(s.header.kernLeftEntryCount) + 1 : nullptr;
  if (!readKernClasses(file, s.kernLeftFileOffset, s.header.kernLeftEntryCount, s.header.kernLeftClassCount, codepoints,
                       cpCount, leftClasses, block, s.kernBlockIndex, s.kernBlockIndexReady) ||
      !readKernClasses(file, s.kernRightFileOffset, s.header.kernRightEntryCount, s.header.kernRightClassCount,
                       codepoints, cpCount, rightClasses, block, rightIndex, s.kernBlockIndexReady)) {
    LOG_ERR("SDCF", "Failed to read kern classes");
    return false;
  }
  s.kernBlockIndexReady = s.kernBlockIndex != nullptr;

  bool usedLeft[256] = {};
  bool usedRight[256] = {};
  uint16_t miniLeftCount = 0;
  uint16_t miniRightCount = 0;
  for (uint32_t i = 0; i < cpCount; i++) {
    if (leftClasses[i]) {
      usedLeft[leftClasses[i]] = true;
      miniLeftCount++;
    }
    if (rightClasses[i]) {
      usedRight[rightClasses[i]] = true;
      miniRightCount++;
    }
  }

  uint8_t leftRenumber[256] = {};
  uint8_t rightRenumber[256] = {};
  uint8_t newToOldLeft[256] = {};
  uint8_t newToOldRight[256] = {};
  uint16_t numLeft = 0, numRight = 0;
  for (int i = 1; i < 256; i++) {
    if (usedLeft[i]) {
      numLeft++;
      leftRenumber[i] = static_cast<uint8_t>(numLeft);
      newToOldLeft[numLeft] = static_cast<uint8_t>(i);
    }
    if (usedRight[i]) {
      numRight++;
      rightRenumber[i] = static_cast<uint8_t>(numRight);
      newToOldRight[numRight] = static_cast<uint8_t>(i);
    }
  }
  if (numLeft == 0 || numRight == 0) {
    return true;
  }

  const uint32_t matrixBytes = static_cast<uint32_t>(numLeft) * numRight;
  s.miniKernLeftClasses = new (std::nothrow) EpdKernClassEntry[miniLeftCount];
  s.miniKernRightClasses = new (std::nothrow) EpdKernClassEntry[miniRightCount];
  s.miniKernMatrix = new (std::nothrow) int8_t[matrixBytes];
  if (!s.miniKernLeftClasses || !s.miniKernRightClasses || !s.miniKernMatrix) {
    LOG_ERR("SDCF", "Failed to allocate mini kern (%u+%u+%u bytes)", miniLeftCount * 3u, miniRightCount * 3u,
            matrixBytes);
    freeStyleMiniKern(s);
    return false;
  }

  uint16_t lIdx = 0, rIdx = 0;
  for (uint32_t i = 0; i < cpCount; i++) {
    const auto cp = static_cast<uint16_t>(codepoints[i]);
    if (leftClasses[i]) {
      s.miniKernLeftClasses[lIdx].codepoint = cp;
      s.miniKernLeftClasses[lIdx].classId = leftRenumber[leftClasses[i]];
      lIdx++;
    }
    if (rightClasses[i]) {
      s.miniKernRightClasses[rIdx].codepoint = cp;
      s.miniKernRightClasses[rIdx].classId = rightRenumber[rightClasses[i]];
      rIdx++;
    }
  }

  const auto* row = reinterpret_cast<const int8_t*>(io);
  for (uint16_t newL = 1; newL <= numLeft; newL++) {
    const uint8_t oldL = newToOldLeft[newL];
    const uint32_t rowFileOff = s.kernMatrixFileOffset + (oldL - 1u) * s.header.kernRightClassCount;
    if (!file.seekSet(rowFileOff)) {
      LOG_ERR("SDCF", "Failed to seek to kern row %u", oldL);
      freeStyleMiniKern(s);
      return false;
    }
    if (file.read(io, s.header.kernRightClassCount) != static_cast<int>(s.header.kernRightClassCount)) {
      LOG_ERR("SDCF", "Failed to read kern row %u", oldL);
      freeStyleMiniKern(s);
      return false;
    }
    int8_t* miniRow = s.miniKernMatrix + (newL - 1u) * numRight;
    for (uint16_t newR = 1; newR <= numRight; newR++) {
      miniRow[newR - 1] = row[newToOldRight[newR] - 1u];
    }
  }

  s.miniKernLeftEntryCount = lIdx;
  s.miniKernRightEntryCount = rIdx;
  s.miniKernLeftClassCount = static_cast<uint8_t>(numLeft);
  s.miniKernRightClassCount = static_cast<uint8_t>(numRight);

  LOG_DBG("SDCF", "Built mini kern: %u×%u matrix (%u bytes, full was %u×%u = %u bytes)", numLeft, numRight, matrixBytes,
          s.header.kernLeftClassCount, s.header.kernRightClassCount,
          static_cast<uint32_t>(s.header.kernLeftClassCount) * s.header.kernRightClassCount);
  return true;
}

// --- Glyph miss callback ---

void SdCardFont::applyGlyphMissCallback(uint8_t styleIdx) {
  overflowCtx_[styleIdx].self = this;
  overflowCtx_[styleIdx].styleIdx = styleIdx;

  auto& s = styleData(styleIdx);
  s.stubData.glyphMissHandler = &SdCardFont::onGlyphMiss;
  s.stubData.glyphMissCtx = &overflowCtx_[styleIdx];
}

// --- Compute per-style file offsets from a base data offset ---

void SdCardFont::computeStyleFileOffsets(PerStyle& s, uint32_t baseOffset) {
  s.intervalsFileOffset = baseOffset;
  s.glyphsFileOffset = s.intervalsFileOffset + s.header.intervalCount * sizeof(EpdUnicodeInterval);
  s.kernLeftFileOffset = s.glyphsFileOffset + s.header.glyphCount * sizeof(EpdGlyph);
  s.kernRightFileOffset = s.kernLeftFileOffset + s.header.kernLeftEntryCount * sizeof(EpdKernClassEntry);
  s.kernMatrixFileOffset = s.kernRightFileOffset + s.header.kernRightEntryCount * sizeof(EpdKernClassEntry);
  s.ligatureFileOffset =
      s.kernMatrixFileOffset + static_cast<uint32_t>(s.header.kernLeftClassCount) * s.header.kernRightClassCount;
  s.bitmapFileOffset = s.ligatureFileOffset + s.header.ligaturePairCount * sizeof(EpdLigaturePair);
}

// --- Load ---

bool SdCardFont::load(const char* path) {
  freeAll();
  if (strlen(path) >= sizeof(filePath_)) {
    LOG_ERR("SDCF", "Path too long (%zu bytes, max %zu)", strlen(path), sizeof(filePath_) - 1);
    return false;
  }
  strncpy(filePath_, path, sizeof(filePath_) - 1);
  filePath_[sizeof(filePath_) - 1] = '\0';

  HalFile file;
  if (!Storage.openFileForRead("SDCF", path, file)) {
    LOG_ERR("SDCF", "Failed to open .cpfont: %s", path);
    return false;
  }
  fileSize_ = static_cast<uint32_t>(file.fileSize());
  corruptGlyphsLogged_ = 0;

  // Read and validate global header
  uint8_t headerBuf[HEADER_SIZE];
  if (file.read(headerBuf, HEADER_SIZE) != HEADER_SIZE) {
    LOG_ERR("SDCF", "Failed to read header");
    return false;
  }

  if (memcmp(headerBuf, CPFONT_MAGIC, 8) != 0) {
    LOG_ERR("SDCF", "Invalid magic bytes");
    return false;
  }

  uint16_t fileVersion = readU16(headerBuf + 8);
  if (fileVersion != CPFONT_VERSION) {
    LOG_ERR("SDCF", "Unsupported version: %u (expected %u)", fileVersion, CPFONT_VERSION);
    return false;
  }

  // Begin content hash: accumulate global header
  uint32_t hash = fnv1a(headerBuf, HEADER_SIZE);

  bool is2Bit = (readU16(headerBuf + 10) & 1) != 0;

  uint8_t styleCount = headerBuf[12];
  if (styleCount == 0 || styleCount > MAX_STYLES) {
    LOG_ERR("SDCF", "Invalid style count: %u", styleCount);
    return false;
  }

  styles_ = makeUniqueNoThrow<PerStyle[]>(styleCount);
  if (!styles_) {
    LOG_ERR("SDCF", "Failed to allocate %zu bytes for style records", styleCount * sizeof(PerStyle));
    return false;
  }
  styleCount_ = styleCount;

  // Read style TOC
  for (uint8_t i = 0; i < styleCount; i++) {
    uint8_t tocBuf[STYLE_TOC_ENTRY_SIZE];
    if (file.read(tocBuf, STYLE_TOC_ENTRY_SIZE) != STYLE_TOC_ENTRY_SIZE) {
      LOG_ERR("SDCF", "Failed to read style TOC entry %u", i);
      freeAll();
      return false;
    }

    // Accumulate TOC entry into content hash
    hash = fnv1a(tocBuf, STYLE_TOC_ENTRY_SIZE, hash);

    uint8_t styleId = tocBuf[0];
    if (styleId >= MAX_STYLES) {
      LOG_ERR("SDCF", "Invalid styleId %u in TOC", styleId);
      file.close();
      freeAll();
      return false;
    }

    if (hasStyle(styleId)) {
      LOG_ERR("SDCF", "Duplicate styleId %u in TOC", styleId);
      freeAll();
      return false;
    }
    styleSlots_[styleId] = i + 1;
    auto& s = styleData(styleId);
    s.header.intervalCount = readU32(tocBuf + 4);
    s.header.glyphCount = readU32(tocBuf + 8);
    s.header.advanceY = tocBuf[12];
    s.header.ascender = readI16(tocBuf + 13);
    s.header.descender = readI16(tocBuf + 15);
    s.header.kernLeftEntryCount = readU16(tocBuf + 17);
    s.header.kernRightEntryCount = readU16(tocBuf + 19);
    s.header.kernLeftClassCount = tocBuf[21];
    s.header.kernRightClassCount = tocBuf[22];
    s.header.ligaturePairCount = tocBuf[23];
    s.header.is2Bit = is2Bit;

    // Sanity-check counts to reject malformed files before allocating.
    // Kern class counts are uint8 (bounded by type). Entry counts are uint16
    // but in practice a sane font has far fewer than 4096 per-side kern entries.
    static constexpr uint32_t MAX_INTERVALS = 4096;
    static constexpr uint32_t MAX_GLYPHS = 65536;
    static constexpr uint32_t MAX_KERN_ENTRIES = 4096;
    if (s.header.intervalCount > MAX_INTERVALS || s.header.glyphCount > MAX_GLYPHS ||
        s.header.kernLeftEntryCount > MAX_KERN_ENTRIES || s.header.kernRightEntryCount > MAX_KERN_ENTRIES) {
      LOG_ERR("SDCF", "Style %u: unreasonable counts (iv=%u, gl=%u, kL=%u, kR=%u)", styleId, s.header.intervalCount,
              s.header.glyphCount, s.header.kernLeftEntryCount, s.header.kernRightEntryCount);
      file.close();
      freeAll();
      return false;
    }

    uint32_t dataOffset = readU32(tocBuf + 24);
    computeStyleFileOffsets(s, dataOffset);

    if (s.header.kernLeftEntryCount > 0 && s.header.kernRightEntryCount > 0) {
      s.kernBlockIndex = new (std::nothrow) uint16_t[kernClassBlocks(s.header.kernLeftEntryCount) + 1 +
                                                     kernClassBlocks(s.header.kernRightEntryCount) + 1];
      if (!s.kernBlockIndex) LOG_DBG("SDCF", "No kern block index for style %u; reading whole class tables", styleId);
    }
  }

  contentHash_ = hash;

  // Load full intervals into RAM for each present style. BMP-only fonts with
  // fewer than 65536 glyphs use a compact 6-byte interval table instead of the
  // on-disk 12-byte table; large sparse CJK subsets otherwise keep tens of KB
  // of always-resident heap just for lookup metadata.
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!hasStyle(i)) continue;
    auto& s = styleData(i);

    if (!file.seekSet(s.intervalsFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek to intervals for style %u", i);
      freeAll();
      return false;
    }

    bool canUseBmp16 = s.header.glyphCount <= UINT16_MAX;
    uint32_t expectedOffset = 0;
    uint32_t prevLast = 0;
    EpdUnicodeInterval iv{};

    uint16_t shareCandidates = 0;
    for (size_t k = 0; k < MAX_SHARED_INTERVALS; k++) {
      if (sharedIntervals[k].table && sharedIntervals[k].count == s.header.intervalCount) {
        shareCandidates |= static_cast<uint16_t>(1u << k);
      }
    }
    for (uint32_t j = 0; j < s.header.intervalCount; ++j) {
      if (file.read(reinterpret_cast<uint8_t*>(&iv), sizeof(iv)) != sizeof(iv)) {
        LOG_ERR("SDCF", "Failed to read interval %u for style %u", j, i);
        freeAll();
        return false;
      }
      if (iv.first > iv.last) {
        LOG_ERR("SDCF", "Style %u: invalid interval %u (first 0x%lX > last 0x%lX)", i, j,
                static_cast<unsigned long>(iv.first), static_cast<unsigned long>(iv.last));
        file.close();
        freeAll();
        return false;
      }
      const uint32_t span = iv.last - iv.first + 1;
      const bool overlapsPrev = (j > 0 && iv.first <= prevLast);
      const bool spanTooBig = (span > s.header.glyphCount);
      const bool offsetMismatch = (iv.offset != expectedOffset);
      const bool offsetOverruns = (iv.offset > s.header.glyphCount - span);
      if (overlapsPrev || spanTooBig || offsetMismatch || offsetOverruns) {
        LOG_ERR("SDCF", "Style %u: invalid interval layout at %u (overlap=%d span=%u offMis=%d offOver=%d)", i, j,
                overlapsPrev, span, offsetMismatch, offsetOverruns);
        file.close();
        freeAll();
        return false;
      }
      if (iv.first > UINT16_MAX || iv.last > UINT16_MAX || iv.offset > UINT16_MAX) {
        canUseBmp16 = false;
      }
      for (size_t k = 0; k < MAX_SHARED_INTERVALS && shareCandidates != 0; k++) {
        if ((shareCandidates & (1u << k)) == 0) continue;
        const auto& shared = sharedIntervals[k];
        bool same;
        if (shared.bmp16) {
          const auto& r = static_cast<const PerStyle::BmpInterval16*>(shared.table)[j];
          same = r.first == iv.first && r.last == iv.last && r.offset == iv.offset;
        } else {
          const auto& r = static_cast<const EpdUnicodeInterval*>(shared.table)[j];
          same = r.first == iv.first && r.last == iv.last && r.offset == iv.offset;
        }
        if (!same) shareCandidates &= static_cast<uint16_t>(~(1u << k));
      }
      expectedOffset += span;
      prevLast = iv.last;
    }

    bool shared = false;
    for (size_t k = 0; k < MAX_SHARED_INTERVALS && shareCandidates != 0; k++) {
      if ((shareCandidates & (1u << k)) == 0) continue;
      auto& entry = sharedIntervals[k];
      if (entry.bmp16 != canUseBmp16) continue;
      if (entry.bmp16) {
        s.bmpIntervals = static_cast<PerStyle::BmpInterval16*>(const_cast<void*>(entry.table));
      } else {
        s.fullIntervals = static_cast<EpdUnicodeInterval*>(const_cast<void*>(entry.table));
      }
      s.intervalsAreBmp16 = entry.bmp16;
      entry.refs++;
      shared = true;
      LOG_DBG("SDCF", "Style %u: sharing a loaded %u-interval table (%u B not allocated)", i, s.header.intervalCount,
              s.header.intervalCount * (canUseBmp16 ? 6u : static_cast<uint32_t>(sizeof(EpdUnicodeInterval))));
      break;
    }

    if (!shared && !file.seekSet(s.intervalsFileOffset)) {
      LOG_ERR("SDCF", "Failed to seek back to intervals for style %u", i);
      freeAll();
      return false;
    }

    if (!shared) {
      if (canUseBmp16) {
        s.bmpIntervals = new (std::nothrow) PerStyle::BmpInterval16[s.header.intervalCount];
        if (!s.bmpIntervals) {
          LOG_ERR("SDCF", "Failed to allocate compact intervals for style %u", i);
          freeAll();
          return false;
        }
        for (uint32_t j = 0; j < s.header.intervalCount; ++j) {
          if (file.read(reinterpret_cast<uint8_t*>(&iv), sizeof(iv)) != sizeof(iv)) {
            LOG_ERR("SDCF", "Failed to read compact interval %u for style %u", j, i);
            freeAll();
            return false;
          }
          s.bmpIntervals[j] = {static_cast<uint16_t>(iv.first), static_cast<uint16_t>(iv.last),
                               static_cast<uint16_t>(iv.offset)};
        }
        s.intervalsAreBmp16 = true;
      } else {
        s.fullIntervals = new (std::nothrow) EpdUnicodeInterval[s.header.intervalCount];
        if (!s.fullIntervals) {
          LOG_ERR("SDCF", "Failed to allocate %u intervals for style %u", s.header.intervalCount, i);
          freeAll();
          return false;
        }
        size_t intervalsBytes = s.header.intervalCount * sizeof(EpdUnicodeInterval);
        if (file.read(reinterpret_cast<uint8_t*>(s.fullIntervals), intervalsBytes) !=
            static_cast<int>(intervalsBytes)) {
          LOG_ERR("SDCF", "Failed to read intervals for style %u", i);
          freeAll();
          return false;
        }
      }
      for (auto& entry : sharedIntervals) {
        if (entry.table) continue;
        entry.table = s.intervalsAreBmp16 ? static_cast<const void*>(s.bmpIntervals) : s.fullIntervals;
        entry.count = s.header.intervalCount;
        entry.bmp16 = s.intervalsAreBmp16;
        entry.refs = 1;
        break;
      }
    }

    memset(&s.stubData, 0, sizeof(s.stubData));
    s.stubData.advanceY = s.header.advanceY;
    s.stubData.ascender = s.header.ascender;
    s.stubData.descender = s.header.descender;
    s.stubData.is2Bit = s.header.is2Bit;

    s.epdFont.data = &s.stubData;
    applyGlyphMissCallback(i);
  }

  loaded_ = true;

  LOG_DBG("SDCF", "Loaded: %s (v%u, %u styles)", path, CPFONT_VERSION, styleCount_);
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!hasStyle(i)) continue;
    const auto& h = styleData(i).header;
    LOG_DBG("SDCF", "  style[%u]: %u intervals, %u glyphs, advY=%u, asc=%d, desc=%d, kernL=%u, kernR=%u, ligs=%u", i,
            h.intervalCount, h.glyphCount, h.advanceY, h.ascender, h.descender, h.kernLeftEntryCount,
            h.kernRightEntryCount, h.ligaturePairCount);
  }
  return true;
}

// --- Codepoint lookup ---

int32_t SdCardFont::findGlobalGlyphIndex(const PerStyle& s, uint32_t codepoint) const {
  int left = 0;
  int right = static_cast<int>(s.header.intervalCount) - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    const uint32_t first = s.intervalsAreBmp16 ? s.bmpIntervals[mid].first : s.fullIntervals[mid].first;
    const uint32_t last = s.intervalsAreBmp16 ? s.bmpIntervals[mid].last : s.fullIntervals[mid].last;
    if (codepoint < first) {
      right = mid - 1;
    } else if (codepoint > last) {
      left = mid + 1;
    } else {
      const uint32_t offset = s.intervalsAreBmp16 ? s.bmpIntervals[mid].offset : s.fullIntervals[mid].offset;
      return static_cast<int32_t>(offset + (codepoint - first));
    }
  }
  return -1;
}

bool SdCardFont::glyphEntryValid(const PerStyle& s, const EpdGlyph& glyph, const uint32_t codepoint) {
  const uint32_t pixels = static_cast<uint32_t>(glyph.width) * glyph.height;
  const uint32_t packed = s.header.is2Bit ? (pixels + 3) / 4 : (pixels + 7) / 8;
  const uint64_t end = static_cast<uint64_t>(s.bitmapFileOffset) + glyph.dataOffset + glyph.dataLength;
  if (glyph.dataLength == packed && end <= fileSize_) return true;
  if (corruptGlyphsLogged_ < 3) {
    corruptGlyphsLogged_++;
    LOG_ERR("SDCF", "Corrupt glyph for U+%04X in %s (%ux%u, %u bytes at %u); drawing it as missing",
            static_cast<unsigned>(codepoint), filePath_, glyph.width, glyph.height, glyph.dataLength,
            static_cast<unsigned>(glyph.dataOffset));
  }
  return false;
}

// --- Prewarm ---

int SdCardFont::prewarm(const char* utf8Text, uint8_t styleMask, bool metadataOnly) {
  if (!loaded_) return -1;
  styleMask = resolveStyleMask(styleMask);
  if (styleMask == 0) return 0;

  unsigned long startMs = millis();

  // Step 1: Extract unique codepoints from UTF-8 text (shared across all styles).
  // Dedup uses O(n^2) linear scan — worst case is MAX_PAGE_GLYPHS (512) unique codepoints
  // = ~131K comparisons, but in practice pages contain far fewer unique codepoints so the
  // actual cost is much lower. This is dwarfed by SD I/O that follows. Alternatives (hash
  // set, bitmap) exceed the 256-byte stack limit or add template bloat.
  // Heap-allocated: MAX_PAGE_GLYPHS * 4 = 2048 bytes, too large for stack (limit < 256 bytes)
  std::unique_ptr<uint32_t[]> codepoints(new (std::nothrow) uint32_t[MAX_PAGE_GLYPHS]);
  if (!codepoints) {
    LOG_ERR("SDCF", "Failed to allocate codepoint buffer (%u bytes)", MAX_PAGE_GLYPHS * 4);
    return -1;
  }
  uint32_t cpCount = 0;

  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8Text);
  while (*p && cpCount < MAX_PAGE_GLYPHS) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;

    bool found = false;
    for (uint32_t i = 0; i < cpCount; i++) {
      if (codepoints[i] == cp) {
        found = true;
        break;
      }
    }
    if (!found) {
      codepoints[cpCount++] = cp;
    }
  }

  // Always include the replacement character
  {
    bool hasReplacement = false;
    for (uint32_t i = 0; i < cpCount; i++) {
      if (codepoints[i] == REPLACEMENT_GLYPH) {
        hasReplacement = true;
        break;
      }
    }
    if (!hasReplacement && cpCount < MAX_PAGE_GLYPHS) {
      codepoints[cpCount++] = REPLACEMENT_GLYPH;
    }
  }

  // Add ligature output codepoints from all styles being prewarmed.
  // Skip during metadata-only prewarm (layout measurement) to avoid loading
  // kern/lig data for all styles upfront (~22KB per style). Kern/lig is
  // loaded per-style in prewarmStyle() during the full render prewarm instead.
  if (!metadataOnly) {
    for (uint8_t si = 0; si < MAX_STYLES; si++) {
      if (!(styleMask & (1 << si)) || !hasStyle(si)) continue;
      auto& s = styleData(si);

      loadStyleLigatures(s);
      if (s.ligaturePairs && s.header.ligaturePairCount > 0) {
        for (uint8_t li = 0; li < s.header.ligaturePairCount && cpCount < MAX_PAGE_GLYPHS; li++) {
          uint32_t leftCp = s.ligaturePairs[li].pair >> 16;
          uint32_t rightCp = s.ligaturePairs[li].pair & 0xFFFF;
          uint32_t outCp = s.ligaturePairs[li].ligatureCp;

          bool hasLeft = false, hasRight = false;
          for (uint32_t i = 0; i < cpCount; i++) {
            if (codepoints[i] == leftCp) hasLeft = true;
            if (codepoints[i] == rightCp) hasRight = true;
            if (hasLeft && hasRight) break;
          }
          if (!hasLeft || !hasRight) continue;

          bool hasOut = false;
          for (uint32_t i = 0; i < cpCount; i++) {
            if (codepoints[i] == outCp) {
              hasOut = true;
              break;
            }
          }
          if (!hasOut) {
            codepoints[cpCount++] = outCp;
          }
        }
      }
    }
  }

  // Sort codepoints for ordered interval building
  std::sort(codepoints.get(), codepoints.get() + cpCount);

  // Prewarm each requested style
  int totalMissed = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (!(styleMask & (1 << si)) || !hasStyle(si)) continue;
    totalMissed += prewarmStyle(si, codepoints.get(), cpCount, metadataOnly);
  }

  stats_.prewarmTotalMs = millis() - startMs;
  return totalMissed;
}

int SdCardFont::prewarmStyle(uint8_t styleIdx, const uint32_t* codepoints, uint32_t cpCount, bool metadataOnly) {
  auto& s = styleData(styleIdx);

  struct CpGlyphMapping {
    uint32_t codepoint;
    int32_t globalIndex;
  };
  auto mappings = makeUniqueNoThrow<CpGlyphMapping[]>(cpCount);
  if (!mappings) {
    LOG_ERR("SDCF", "Failed to allocate mapping array for style %u", styleIdx);
    return static_cast<int>(cpCount);
  }

  uint32_t validCount = 0;
  for (uint32_t i = 0; i < cpCount; i++) {
    int32_t idx = findGlobalGlyphIndex(s, codepoints[i]);
    if (idx >= 0) {
      mappings[validCount].codepoint = codepoints[i];
      mappings[validCount].globalIndex = idx;
      validCount++;
    }
  }
  int missed = static_cast<int>(cpCount - validCount);

  if (validCount == 0) {
    freeStyleMiniData(s);
    s.epdFont.data = &s.stubData;
    return missed;
  }

  freeStyleMiniData(s);

  uint32_t intervalCapacity = validCount;
  s.miniIntervals = new (std::nothrow) EpdUnicodeInterval[intervalCapacity];
  if (!s.miniIntervals) {
    LOG_ERR("SDCF", "Failed to allocate mini intervals for style %u", styleIdx);
    return static_cast<int>(cpCount);
  }

  s.miniIntervalCount = 0;
  uint32_t rangeStart = 0;
  for (uint32_t i = 1; i <= validCount; i++) {
    if (i == validCount || mappings[i].codepoint != mappings[i - 1].codepoint + 1) {
      s.miniIntervals[s.miniIntervalCount].first = mappings[rangeStart].codepoint;
      s.miniIntervals[s.miniIntervalCount].last = mappings[i - 1].codepoint;
      s.miniIntervals[s.miniIntervalCount].offset = rangeStart;
      s.miniIntervalCount++;
      rangeStart = i;
    }
  }

  s.miniGlyphCount = validCount;
  s.miniGlyphs = new (std::nothrow) EpdGlyph[s.miniGlyphCount];
  if (!s.miniGlyphs) {
    LOG_ERR("SDCF", "Failed to allocate mini glyphs for style %u", styleIdx);
    freeStyleMiniData(s);
    return static_cast<int>(cpCount);
  }

  auto readOrder = makeUniqueNoThrow<uint32_t[]>(validCount);
  if (!readOrder) {
    LOG_ERR("SDCF", "Failed to allocate read order for style %u", styleIdx);
    freeStyleMiniData(s);
    return static_cast<int>(cpCount);
  }
  for (uint32_t i = 0; i < validCount; i++) readOrder[i] = i;
  std::sort(readOrder.get(), readOrder.get() + validCount,
            [&](uint32_t a, uint32_t b) { return mappings[a].globalIndex < mappings[b].globalIndex; });

  HalFile file;
  if (!Storage.openFileForRead("SDCF", filePath_, file)) {
    LOG_ERR("SDCF", "Failed to reopen .cpfont for prewarm (style %u)", styleIdx);
    freeStyleMiniData(s);
    return static_cast<int>(cpCount);
  }

  unsigned long sdStart = millis();
  uint32_t seekCount = 0;

  int32_t lastReadIndex = INT32_MIN;
  for (uint32_t i = 0; i < validCount; i++) {
    uint32_t mapIdx = readOrder[i];
    int32_t gIdx = mappings[mapIdx].globalIndex;

    uint32_t fileOff = s.glyphsFileOffset + static_cast<uint32_t>(gIdx) * sizeof(EpdGlyph);
    if (gIdx != lastReadIndex + 1) {
      if (!file.seekSet(fileOff)) {
        LOG_ERR("SDCF", "Prewarm: failed to seek to glyph %d (style %u)", gIdx, styleIdx);
        file.close();
        freeStyleMiniData(s);
        return static_cast<int>(cpCount);
      }
      seekCount++;
    }
    if (file.read(reinterpret_cast<uint8_t*>(&s.miniGlyphs[mapIdx]), sizeof(EpdGlyph)) != sizeof(EpdGlyph)) {
      LOG_ERR("SDCF", "Prewarm: short glyph read (style %u, glyph %d)", styleIdx, gIdx);
      freeStyleMiniData(s);
      return static_cast<int>(cpCount);
    }
    if (!glyphEntryValid(s, s.miniGlyphs[mapIdx], mappings[mapIdx].codepoint)) s.miniGlyphs[mapIdx] = EpdGlyph{};
    lastReadIndex = gIdx;
  }

  mappings.reset();

  uint32_t totalBitmapSize = 0;

  if (!metadataOnly) {
    for (uint32_t i = 0; i < validCount; i++) {
      totalBitmapSize += s.miniGlyphs[i].dataLength;
    }

    readOrder.reset();
    const uint32_t bitmapAlloc = totalBitmapSize > 0 ? totalBitmapSize : 1;
    s.miniBitmap = new (std::nothrow) uint8_t[bitmapAlloc];
    if (!s.miniBitmap && hasAdvanceTable()) {
      const uint32_t before = ESP.getMaxAllocHeap();
      clearPersistentCache();
      LOG_DBG("SDCF", "Retrying bitmap after advance-cache release: maxAlloc=%u -> %u", static_cast<unsigned>(before),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
      s.miniBitmap = new (std::nothrow) uint8_t[bitmapAlloc];
    }
    if (!s.miniBitmap) {
      LOG_ERR("SDCF", "Failed to allocate mini bitmap (%u bytes) for style %u", totalBitmapSize, styleIdx);
      freeStyleMiniData(s);
      return static_cast<int>(cpCount);
    }

    readOrder = makeUniqueNoThrow<uint32_t[]>(validCount);
    if (!readOrder) {
      LOG_ERR("SDCF", "Failed to allocate bitmap read order for style %u", styleIdx);
      freeStyleMiniData(s);
      return static_cast<int>(cpCount);
    }
    for (uint32_t i = 0; i < validCount; i++) readOrder[i] = i;
    std::sort(readOrder.get(), readOrder.get() + validCount,
              [&](uint32_t a, uint32_t b) { return s.miniGlyphs[a].dataOffset < s.miniGlyphs[b].dataOffset; });

    uint32_t miniBitmapOffset = 0;
    uint32_t lastBitmapEnd = UINT32_MAX;
    for (uint32_t i = 0; i < validCount; i++) {
      uint32_t mapIdx = readOrder[i];
      EpdGlyph& glyph = s.miniGlyphs[mapIdx];

      if (glyph.dataLength == 0) {
        glyph.dataOffset = miniBitmapOffset;
        continue;
      }

      uint32_t fileOff = s.bitmapFileOffset + glyph.dataOffset;
      if (fileOff != lastBitmapEnd) {
        if (!file.seekSet(fileOff)) {
          LOG_ERR("SDCF", "Prewarm: failed to seek to bitmap (style %u)", styleIdx);
          file.close();
          freeStyleMiniData(s);
          return static_cast<int>(cpCount);
        }
        seekCount++;
      }
      if (file.read(s.miniBitmap + miniBitmapOffset, glyph.dataLength) != static_cast<int>(glyph.dataLength)) {
        LOG_ERR("SDCF", "Prewarm: short bitmap read (style %u)", styleIdx);
        freeStyleMiniData(s);
        return static_cast<int>(cpCount);
      }
      lastBitmapEnd = fileOff + glyph.dataLength;

      glyph.dataOffset = miniBitmapOffset;
      miniBitmapOffset += glyph.dataLength;
    }
  }

  uint32_t sdTime = millis() - sdStart;
  readOrder.reset();

  bool ligaturesOk = false;
  if (!metadataOnly) {
    ligaturesOk = loadStyleLigatures(s);
    if (ligaturesOk) {
      buildMiniKernMatrix(s, codepoints, cpCount);
    }
  }

  memset(&s.miniData, 0, sizeof(s.miniData));
  s.miniData.bitmap = s.miniBitmap;
  s.miniData.glyph = s.miniGlyphs;
  s.miniData.intervals = s.miniIntervals;
  s.miniData.intervalCount = s.miniIntervalCount;
  s.miniData.advanceY = s.header.advanceY;
  s.miniData.ascender = s.header.ascender;
  s.miniData.descender = s.header.descender;
  s.miniData.is2Bit = s.header.is2Bit;
  if (ligaturesOk) {
    applyKernLigaturePointers(s, s.miniData);
  }
  s.miniData.glyphMissHandler = &SdCardFont::onGlyphMiss;
  s.miniData.glyphMissCtx = &overflowCtx_[styleIdx];

  s.epdFont.data = &s.miniData;

  stats_.sdReadTimeMs += sdTime;
  stats_.seekCount += seekCount;
  stats_.uniqueGlyphs += validCount;
  stats_.bitmapBytes += totalBitmapSize;

  return missed;
}

// --- Cache management ---

void SdCardFont::clearCache() {
  clearOverflow();
  // Note: advance table is intentionally preserved here. It persists across
  // layout passes so repeated section indexing amortizes SD reads. Use
  // clearPersistentCache() to wipe it.
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (!hasStyle(i)) continue;
    freeStyleMiniData(styleData(i));
    applyGlyphMissCallback(i);
  }
}

// --- Advance table ---

void SdCardFont::clearPersistentCache() {
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    delete[] advanceTable_[i];
    advanceTable_[i] = nullptr;
    advanceTableSize_[i] = 0;
    advanceTableCapacity_[i] = 0;
  }
}

bool SdCardFont::advanceTableLookup(uint8_t styleIdx, uint32_t codepoint, uint16_t* outAdvance) const {
  const AdvanceEntry* table = advanceTable_[styleIdx];
  const uint32_t size = advanceTableSize_[styleIdx];
  if (!table || size == 0) return false;
  uint32_t lo = 0, hi = size;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (table[mid].codepoint < codepoint) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < size && table[lo].codepoint == codepoint) {
    if (outAdvance) *outAdvance = table[lo].advanceX;
    return true;
  }
  return false;
}

bool SdCardFont::growAdvanceTable(const uint8_t styleIdx, const uint32_t needed) {
  if (needed <= advanceTableCapacity_[styleIdx]) return true;
  constexpr uint32_t MIN_ADVANCE_CAPACITY = 64;
  uint32_t capacity = std::max({needed, advanceTableCapacity_[styleIdx] * 2, MIN_ADVANCE_CAPACITY});
  capacity = std::min(capacity, ADVANCE_CACHE_LIMIT);
  AdvanceEntry* grown = new (std::nothrow) AdvanceEntry[capacity];
  if (!grown && capacity > needed) {
    capacity = needed;
    grown = new (std::nothrow) AdvanceEntry[capacity];
  }
  if (!grown) return false;
  if (advanceTableSize_[styleIdx] > 0) {
    memcpy(grown, advanceTable_[styleIdx], advanceTableSize_[styleIdx] * sizeof(AdvanceEntry));
  }
  delete[] advanceTable_[styleIdx];
  advanceTable_[styleIdx] = grown;
  advanceTableCapacity_[styleIdx] = capacity;
  return true;
}

void SdCardFont::mergeIntoAdvanceTable(uint8_t styleIdx, const AdvanceEntry* sortedNew, uint32_t newCount) {
  if (newCount == 0) return;
  const uint32_t oldSize = advanceTableSize_[styleIdx];
  if (oldSize >= ADVANCE_CACHE_LIMIT) return;

  uint32_t mergedCap = oldSize + newCount;
  if (mergedCap > ADVANCE_CACHE_LIMIT) mergedCap = ADVANCE_CACHE_LIMIT;

  if (!growAdvanceTable(styleIdx, mergedCap)) {
    LOG_ERR("SDCF", "mergeIntoAdvanceTable: alloc failed (%u entries) style %u", mergedCap, styleIdx);
    return;
  }

  AdvanceEntry* table = advanceTable_[styleIdx];
  uint32_t drop = oldSize + newCount - mergedCap;
  uint32_t i = oldSize, j = newCount, k = oldSize + newCount;
  while (j > 0) {
    const bool takeNew = i == 0 || sortedNew[j - 1].codepoint >= table[i - 1].codepoint;
    const AdvanceEntry entry = takeNew ? sortedNew[--j] : table[--i];
    --k;
    if (drop > 0) {
      --drop;
    } else {
      table[k] = entry;
    }
  }
  advanceTableSize_[styleIdx] = mergedCap;
}

bool SdCardFont::hasAdvanceTable() const {
  for (uint8_t i = 0; i < MAX_STYLES; i++) {
    if (advanceTableSize_[i] > 0) return true;
  }
  return false;
}

uint16_t SdCardFont::getAdvance(uint32_t codepoint, uint8_t style) const {
  style &= (MAX_STYLES - 1);
  if (!advanceTable_[style]) return 0;
  const AdvanceEntry* table = advanceTable_[style];
  const uint32_t size = advanceTableSize_[style];
  // Binary search sorted by codepoint
  uint32_t lo = 0, hi = size;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (table[mid].codepoint < codepoint) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < size && table[lo].codepoint == codepoint) {
    return table[lo].advanceX;
  }
  return 0;
}

// Given a sorted array of unique codepoints, resolve glyph indices per style,
// batch-read advanceX from SD, and merge into the persistent advance table.
// Caller owns the codepoints buffer.
int SdCardFont::fetchAdvancesForCodepoints(uint32_t* codepoints, uint32_t cpCount, uint8_t styleMask) {
  int totalMissed = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (!(styleMask & (1 << si)) || !hasStyle(si)) continue;
    const auto& s = styleData(si);

    // Stop fetching once the cache is full — further inserts would be dropped
    // by the merge anyway. The renderer fast path tolerates missing entries
    // (returns 0); the slow path is still correct for those codepoints.
    if (advanceTableSize_[si] >= ADVANCE_CACHE_LIMIT) continue;

    // For each codepoint in `codepoints`, skip those already cached, then
    // resolve to a glyph index. Build a parallel array sorted by glyph index
    // for sequential SD reads.
    struct CpIdx {
      uint32_t codepoint;
      int32_t glyphIndex;
    };
    std::unique_ptr<CpIdx[]> mappings(new (std::nothrow) CpIdx[cpCount]);
    if (!mappings) {
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate mappings for style %u", si);
      totalMissed += cpCount;
      continue;
    }

    uint32_t needCount = 0;
    uint32_t missedThisStyle = 0;
    const int32_t replacementIdx = findGlobalGlyphIndex(s, REPLACEMENT_GLYPH);
    for (uint32_t i = 0; i < cpCount; i++) {
      const uint32_t cp = codepoints[i];
      if (advanceTableLookup(si, cp, nullptr)) continue;  // already cached
      int32_t idx = findGlobalGlyphIndex(s, cp);
      if (idx < 0) {
        if (replacementIdx < 0) {
          missedThisStyle++;
          continue;
        }
        idx = replacementIdx;
      }
      mappings[needCount].codepoint = cp;
      mappings[needCount].glyphIndex = idx;
      needCount++;
    }
    totalMissed += static_cast<int>(missedThisStyle);

    if (needCount == 0) continue;

    // Sort by glyph index so SD reads are mostly sequential.
    std::sort(mappings.get(), mappings.get() + needCount,
              [](const CpIdx& a, const CpIdx& b) { return a.glyphIndex < b.glyphIndex; });

    // Open file once and read advanceX for each needed glyph.
    HalFile file;
    if (!Storage.openFileForRead("SDCF", filePath_, file)) {
      LOG_ERR("SDCF", "buildAdvanceTable: failed to open .cpfont for style %u", si);
      continue;
    }

    std::unique_ptr<AdvanceEntry[]> staged(new (std::nothrow) AdvanceEntry[needCount]);
    if (!staged) {
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate staging for style %u", si);
      file.close();
      continue;
    }

    uint32_t fetched = 0;
    EpdGlyph tempGlyph;
    int32_t lastReadIndex = INT32_MIN;
    for (uint32_t i = 0; i < needCount; i++) {
      int32_t gIdx = mappings[i].glyphIndex;
      uint32_t fileOff = s.glyphsFileOffset + static_cast<uint32_t>(gIdx) * sizeof(EpdGlyph);
      if (gIdx != lastReadIndex + 1) {
        if (!file.seekSet(fileOff)) {
          LOG_ERR("SDCF", "buildAdvanceTable: failed to seek to glyph %d (style %u)", gIdx, si);
          break;
        }
      }
      if (file.read(reinterpret_cast<uint8_t*>(&tempGlyph), sizeof(EpdGlyph)) != sizeof(EpdGlyph)) {
        LOG_ERR("SDCF", "buildAdvanceTable: short glyph read (style %u, glyph %d)", si, gIdx);
        break;
      }
      lastReadIndex = gIdx;
      staged[fetched].codepoint = mappings[i].codepoint;
      staged[fetched].advanceX = glyphEntryValid(s, tempGlyph, mappings[i].codepoint) ? tempGlyph.advanceX : 0;
      fetched++;
    }
    file.close();

    if (fetched > 0) {
      // Sort staged by codepoint, then merge into the persistent table.
      std::sort(staged.get(), staged.get() + fetched,
                [](const AdvanceEntry& a, const AdvanceEntry& b) { return a.codepoint < b.codepoint; });
      mergeIntoAdvanceTable(si, staged.get(), fetched);
    }

    LOG_DBG("SDCF", "Advance table style %u: +%u from SD, total=%u/%u", si, fetched, advanceTableSize_[si],
            ADVANCE_CACHE_LIMIT);
  }

  return totalMissed;
}

template <typename Iter>
int SdCardFont::buildAdvanceTableRange(Iter begin, Iter end, bool includeSpace, bool includeHyphen, uint8_t styleMask) {
  if (!loaded_) return -1;
  styleMask = resolveStyleMask(styleMask);
  if (styleMask == 0) return 0;

  unsigned long startMs = millis();

  uint8_t openStyles = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if ((styleMask & (1 << si)) && hasStyle(si) && advanceTableSize_[si] < ADVANCE_CACHE_LIMIT) {
      openStyles |= 1 << si;
    }
  }
  if (openStyles == 0) {
    stats_.prewarmTotalMs = millis() - startMs;
    return 0;
  }

  constexpr uint32_t ADVANCE_GROWTH_HEADROOM = 64;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (!(openStyles & (1 << si))) continue;
    const uint32_t size = advanceTableSize_[si];
    if (size < ADVANCE_CACHE_LIMIT && advanceTableCapacity_[si] - size < ADVANCE_GROWTH_HEADROOM) {
      if (!growAdvanceTable(si, std::min(size + ADVANCE_GROWTH_HEADROOM, ADVANCE_CACHE_LIMIT))) {
        LOG_DBG("SDCF", "Advance table pre-grow failed for style %u; the merge grows it", si);
      }
    }
  }

  size_t limit = 0;
  for (auto it = begin; it != end; ++it) limit += strlen(asCStr(*it));
  limit = std::min<size_t>(limit, ADVANCE_CACHE_LIMIT);
  static constexpr size_t INLINE_CODEPOINTS = 32;
  uint32_t inlineCodepoints[INLINE_CODEPOINTS + 2];
  std::unique_ptr<uint32_t[]> heapCodepoints;
  uint32_t* codepoints = inlineCodepoints;
  if (limit > INLINE_CODEPOINTS) {
    heapCodepoints = makeUniqueNoThrow<uint32_t[]>(limit + 2);
    if (!heapCodepoints) {
      LOG_ERR("SDCF", "buildAdvanceTable: failed to allocate codepoint buffer (%u bytes)",
              static_cast<unsigned>((limit + 2) * sizeof(uint32_t)));
      return -1;
    }
    codepoints = heapCodepoints.get();
  }

  int totalMissed = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (!(openStyles & (1 << si))) continue;
    const PerStyle& style = styleData(si);
    const bool hasReplacement = findGlobalGlyphIndex(style, REPLACEMENT_GLYPH) >= 0;
    const auto resolves = [&](const uint32_t cp) { return hasReplacement || findGlobalGlyphIndex(style, cp) >= 0; };
    const auto collect = [&](LowestCodepoints& into, auto wanted) {
      for (auto it = begin; it != end; ++it) {
        collectCodepoints(asCStr(*it), into, wanted);
      }
      if (includeSpace && wanted(' ')) into.addReserved(' ');
      if (includeHyphen && wanted('-')) into.addReserved('-');
    };

    bool anyLacking = false;
    LowestCodepoints collected(codepoints, static_cast<uint32_t>(limit));
    collect(collected, [&](const uint32_t cp) {
      if (advanceTableLookup(si, cp, nullptr)) return false;
      if (resolves(cp)) return true;
      anyLacking = true;
      return false;
    });
    std::sort(collected.data(), collected.data() + collected.size());
    totalMissed += fetchAdvancesForCodepoints(collected.data(), collected.size(), 1 << si);

    if (anyLacking) {
      LowestCodepoints lacking(codepoints, static_cast<uint32_t>(limit));
      collect(lacking, [&](const uint32_t cp) { return !resolves(cp); });
      totalMissed += static_cast<int>(lacking.size());
    }
  }
  stats_.prewarmTotalMs = millis() - startMs;
  return totalMissed;
}

int SdCardFont::buildAdvanceTable(const char* utf8Text, uint8_t styleMask) {
  return buildAdvanceTableRange(&utf8Text, &utf8Text + 1, false, false, styleMask);
}

int SdCardFont::buildAdvanceTable(const std::vector<std::string>& words, bool includeHyphen, uint8_t styleMask) {
  return buildAdvanceTableRange(words.begin(), words.end(), words.size() > 1, includeHyphen, styleMask);
}

// --- Stats ---

void SdCardFont::logStats(const char* label) {
  LOG_DBG("SDCF", "[%s] total=%ums sd_read=%ums seeks=%u glyphs=%u bitmap=%u bytes", label, stats_.prewarmTotalMs,
          stats_.sdReadTimeMs, stats_.seekCount, stats_.uniqueGlyphs, stats_.bitmapBytes);
}

void SdCardFont::resetStats() { stats_ = Stats{}; }

// --- Public accessors ---

EpdFont* SdCardFont::getEpdFont(uint8_t style) {
  style &= (MAX_STYLES - 1);
  if (!hasStyle(style)) return nullptr;
  return &styleData(style).epdFont;
}

bool SdCardFont::hasStyle(uint8_t style) const { return styleSlots_[style & (MAX_STYLES - 1)] != 0; }

uint8_t SdCardFont::resolveStyle(uint8_t style) const {
  static const uint8_t kFallbacks[MAX_STYLES][MAX_STYLES] = {
      // REGULAR: REGULAR -> BOLD -> ITALIC -> BOLD_ITALIC
      {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC},
      // BOLD: BOLD -> REGULAR -> BOLD_ITALIC -> ITALIC
      {EpdFontFamily::BOLD, EpdFontFamily::REGULAR, EpdFontFamily::BOLD_ITALIC, EpdFontFamily::ITALIC},
      // ITALIC: ITALIC -> REGULAR -> BOLD_ITALIC -> BOLD
      {EpdFontFamily::ITALIC, EpdFontFamily::REGULAR, EpdFontFamily::BOLD_ITALIC, EpdFontFamily::BOLD},
      // BOLD_ITALIC: BOLD_ITALIC -> BOLD -> ITALIC -> REGULAR
      {EpdFontFamily::BOLD_ITALIC, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::REGULAR},
  };

  const uint8_t styleBits = style & (MAX_STYLES - 1);
  for (uint8_t candidate : kFallbacks[styleBits]) {
    if (hasStyle(candidate)) return candidate;
  }
  return EpdFontFamily::REGULAR;
}

uint8_t SdCardFont::resolveStyleMask(uint8_t styleMask) const {
  uint8_t resolvedMask = 0;
  for (uint8_t si = 0; si < MAX_STYLES; si++) {
    if (styleMask & (1 << si)) {
      resolvedMask |= static_cast<uint8_t>(1u << resolveStyle(si));
    }
  }
  return resolvedMask;
}

// --- On-demand glyph loading (overflow buffer) ---

const EpdGlyph* SdCardFont::onGlyphMiss(void* ctx, uint32_t codepoint) {
  auto* oc = static_cast<OverflowContext*>(ctx);
  auto* self = oc->self;
  uint8_t styleIdx = oc->styleIdx;

  if (!self->loaded_ || styleIdx >= MAX_STYLES || !self->hasStyle(styleIdx)) return nullptr;
  const auto& s = self->styleData(styleIdx);
  if (!s.fullIntervals && !s.bmpIntervals) return nullptr;

  // Check overflow cache first (matching both codepoint and style)
  for (uint32_t i = 0; i < self->overflowCount_; i++) {
    if (self->overflow_[i].codepoint == codepoint && self->overflow_[i].styleIdx == styleIdx) {
      return &self->overflow_[i].glyph;
    }
  }

  // Look up global glyph index via full intervals
  int32_t globalIdx = self->findGlobalGlyphIndex(s, codepoint);
  if (globalIdx < 0) return nullptr;

  // Pick overflow slot (ring buffer). Read into temporaries first so the
  // existing slot stays valid if SD I/O fails. Bookkeeping (count/next)
  // is deferred until after all I/O succeeds to avoid inconsistent state.
  uint32_t slot = self->overflowNext_;
  bool wasAtCapacity = (self->overflowCount_ == OVERFLOW_CAPACITY);

  // Read glyph metadata into temporary
  HalFile file;
  if (!Storage.openFileForRead("SDCF", self->filePath_, file)) {
    LOG_ERR("SDCF", "Overflow: failed to open .cpfont");
    return nullptr;
  }

  EpdGlyph tempGlyph = {};
  uint32_t glyphFileOff = s.glyphsFileOffset + static_cast<uint32_t>(globalIdx) * sizeof(EpdGlyph);
  if (!file.seekSet(glyphFileOff)) {
    LOG_ERR("SDCF", "Overflow: failed to seek to glyph for U+%04X style %u", codepoint, styleIdx);
    file.close();
    return nullptr;
  }
  if (file.read(reinterpret_cast<uint8_t*>(&tempGlyph), sizeof(EpdGlyph)) != sizeof(EpdGlyph)) {
    LOG_ERR("SDCF", "Overflow: failed to read glyph metadata for U+%04X style %u", codepoint, styleIdx);
    return nullptr;
  }
  if (!self->glyphEntryValid(s, tempGlyph, codepoint)) tempGlyph = EpdGlyph{};

  // Read bitmap data into temporary (if any)
  uint8_t* tempBitmap = nullptr;
  if (tempGlyph.dataLength > 0) {
    tempBitmap = new (std::nothrow) uint8_t[tempGlyph.dataLength];
    if (!tempBitmap) {
      LOG_ERR("SDCF", "Overflow: failed to allocate %u bytes for U+%04X bitmap", tempGlyph.dataLength, codepoint);
      return nullptr;
    }
    if (!file.seekSet(s.bitmapFileOffset + tempGlyph.dataOffset)) {
      LOG_ERR("SDCF", "Overflow: failed to seek to bitmap for U+%04X", codepoint);
      delete[] tempBitmap;
      file.close();
      return nullptr;
    }
    if (file.read(tempBitmap, tempGlyph.dataLength) != static_cast<int>(tempGlyph.dataLength)) {
      LOG_ERR("SDCF", "Overflow: failed to read bitmap for U+%04X", codepoint);
      delete[] tempBitmap;
      return nullptr;
    }
  }

  // All reads succeeded — commit to slot and advance ring buffer
  if (wasAtCapacity) {
    delete[] self->overflow_[slot].bitmap;
  } else {
    self->overflowCount_++;
  }
  self->overflowNext_ = (slot + 1) % OVERFLOW_CAPACITY;
  self->overflow_[slot].glyph = tempGlyph;
  self->overflow_[slot].bitmap = tempBitmap;
  self->overflow_[slot].codepoint = codepoint;
  self->overflow_[slot].styleIdx = styleIdx;

  LOG_DBG("SDCF", "Overflow: loaded U+%04X style %u on demand (slot %u/%u)", codepoint, styleIdx, slot,
          OVERFLOW_CAPACITY);

  return &self->overflow_[slot].glyph;
}

bool SdCardFont::isOverflowGlyph(const EpdGlyph* glyph) const {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    if (&overflow_[i].glyph == glyph) return true;
  }
  return false;
}

const uint8_t* SdCardFont::getOverflowBitmap(const EpdGlyph* glyph) const {
  for (uint32_t i = 0; i < overflowCount_; i++) {
    if (&overflow_[i].glyph == glyph) {
      return overflow_[i].bitmap;
    }
  }
  return nullptr;
}

SdCardFont* SdCardFont::fromMissCtx(void* ctx) { return static_cast<OverflowContext*>(ctx)->self; }
