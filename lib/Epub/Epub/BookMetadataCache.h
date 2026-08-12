#pragma once

#include <HalStorage.h>
#include <Serialization.h>

#include <algorithm>
#include <deque>
#include <string>

class BookMetadataCache {
 public:
  // book.bin format version. Bump when the binary layout or the stored string
  // content changes. Consumers outside this class (BookProgressDataStore's
  // lightweight parser) validate against this same constant so the two can't
  // drift apart again (Pokemon levels froze at Lv1 when this hit v6+ while the
  // parser still expected v5).
  // v9: ambiguous guide type="text" references are no longer stored as the
  // reading start location, so a v8 cache carries a textReferenceHref this
  // build would never have written.
  static constexpr uint8_t kFormatVersion = 9;

  struct BookMetadata {
    std::string title;
    std::string author;
    std::string language;
    std::string coverItemHref;
    std::string textReferenceHref;
  };

  struct SpineEntry {
    std::string href;
    uint32_t cumulativeSize;
    int16_t tocIndex;

    SpineEntry() : cumulativeSize(0), tocIndex(-1) {}
    SpineEntry(std::string href, const uint32_t cumulativeSize, const int16_t tocIndex)
        : href(std::move(href)), cumulativeSize(cumulativeSize), tocIndex(tocIndex) {}

    // Enable comparison for memo cache invalidation
    bool operator==(const SpineEntry& other) const {
      return href == other.href && cumulativeSize == other.cumulativeSize && tocIndex == other.tocIndex;
    }
  };

  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
    int16_t spineIndex;

    TocEntry() : level(0), spineIndex(-1) {}
    TocEntry(std::string title, std::string href, std::string anchor, const uint8_t level, const int16_t spineIndex)
        : title(std::move(title)),
          href(std::move(href)),
          anchor(std::move(anchor)),
          level(level),
          spineIndex(spineIndex) {}

    // Enable comparison for memo cache invalidation
    bool operator==(const TocEntry& other) const {
      return title == other.title && href == other.href && anchor == other.anchor && level == other.level &&
             spineIndex == other.spineIndex;
    }
  };

 private:
  std::string cachePath;
  uint32_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  bool loaded;
  bool buildMode;

  // One-entry memo cache for spine and toc entries (avoid repeated SD seeks)
  int cachedSpineIndex = -1;
  SpineEntry cachedSpineEntry;
  int cachedTocIndex = -1;
  TocEntry cachedTocEntry;

  void invalidateMemoCache() {
    cachedSpineIndex = -1;
    cachedTocIndex = -1;
  }

  HalFile bookFile;
  // Temp file handles during build
  HalFile spineFile;
  HalFile tocFile;

  // Index for fast href→spineIndex lookup (used only for large EPUBs)
  struct SpineHrefIndexEntry {
    uint64_t hrefHash;  // FNV-1a 64-bit hash
    uint16_t hrefLen;   // length for collision reduction
    int16_t spineIndex;
  };
  std::deque<SpineHrefIndexEntry> spineHrefIndex;
  bool useSpineHrefIndex = false;

  static constexpr uint16_t LARGE_SPINE_THRESHOLD = 400;

  // FNV-1a 64-bit hash function
  static uint64_t fnvHash64(const std::string& s) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  uint32_t writeSpineEntry(HalFile& file, const SpineEntry& entry) const;
  uint32_t writeTocEntry(HalFile& file, const TocEntry& entry) const;
  SpineEntry readSpineEntry(HalFile& file) const;
  TocEntry readTocEntry(HalFile& file) const;
  // Overloads accepting BufferedReader for efficient batched reads
  SpineEntry readSpineEntry(serialization::BufferedReader& reader) const;
  TocEntry readTocEntry(serialization::BufferedReader& reader) const;

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath)
      : cachePath(std::move(cachePath)), lutOffset(0), spineCount(0), tocCount(0), loaded(false), buildMode(false) {}
  ~BookMetadataCache() = default;

  // Building phase (stream to disk immediately)
  bool beginWrite();
  bool beginContentOpfPass();
  void createSpineEntry(const std::string& href);
  bool endContentOpfPass();
  bool beginTocPass();
  void createTocEntry(const std::string& title, const std::string& href, const std::string& anchor, uint8_t level);
  bool endTocPass();
  bool endWrite();
  bool cleanupTmpFiles() const;

  // Post-processing to update mappings and sizes
  bool buildBookBin(const std::string& epubPath, const BookMetadata& metadata);

  // Reading phase (read mode)
  bool load();
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  int getSpineCount() const { return spineCount; }
  int getTocCount() const { return tocCount; }
  bool isLoaded() const { return loaded; }
};
