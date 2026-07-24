#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct RecentBook;
class HalFile;

namespace homecarousel {

struct CacheGeometry {
  uint32_t frameBufferSize = 0;
  uint16_t screenWidth = 0;
  uint16_t screenHeight = 0;
  uint16_t centerCoverW = 0;
  uint16_t centerCoverH = 0;
  uint16_t sideCoverW = 0;
  uint16_t sideCoverH = 0;
};

// Keep this layout byte-identical with the existing home_carousel_cache.bin
// header. It is serialized as a POD, including the target ABI's padding.
struct CacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t frameCount;
  uint32_t frameBufferSize;
  uint64_t keyHash;
  uint16_t screenWidth;
  uint16_t screenHeight;
  uint16_t centerCoverW;
  uint16_t centerCoverH;
  uint16_t sideCoverW;
  uint16_t sideCoverH;
  // First 8 bytes of the running firmware's ELF SHA-256. Frames bake in menu/
  // layout that any firmware change can alter, so a new firmware invalidates the
  // cache even when books, geometry, and menu signature are unchanged ("invalidate
  // on flash"). 0 on host/simulator builds (no firmware image).
  uint64_t appFingerprint;
};

using CoverStateLookup = void (*)(void* context, const RecentBook& book, bool& centerExists, bool& sideExists);
using FrameRenderer = void (*)(void* context, int bookIdx);
using BufferReleaser = void (*)(void* context);
using ProgressReporter = void (*)(void* context, int percent);

class HomeCarouselCache {
 public:
  static constexpr int kFrameCount = 1;
  static constexpr size_t kHeadroom = 4096;
  static constexpr uint32_t kMagic = 0x43434152;  // "CCAR"
  static constexpr uint16_t kVersion = 3;         // bumped: header gained appFingerprint
  static constexpr const char* kCachePath = "/.crosspoint/home_carousel_cache.bin";
  static constexpr const char* kCacheTmpPath = "/.crosspoint/home_carousel_cache.tmp";

  uint8_t* frames[kFrameCount] = {};
  int frameBookIdx[kFrameCount] = {-1};
  int frameCount = 0;
  int lastCenterIdx = -1;
  std::string key;
  uint64_t keyHash = 0;

  ~HomeCarouselCache();

  static HomeCarouselCache& shared();
  static bool canAllocateFrameBuffer(size_t bufferSize, size_t freeHeap);
  // menuSignature encodes the current home menu layout. Carousel frames bake in
  // the menu icon row, so a menu change (e.g. feature flags, library unification)
  // must invalidate otherwise-identical (same books) cached frames.
  static void buildCacheKey(const std::vector<RecentBook>& recentBooks, std::string& key, uint64_t& keyHash,
                            CoverStateLookup lookup, void* context, const std::string& menuSignature = std::string());

  int findFrameSlot(int bookIdx) const;
  void invalidate();
  void releaseSlot(int slotIdx);
  void demoteToDiskOnly();
  bool allocateFrameSlots(int targetFrameCount, size_t bufferSize, size_t freeHeap);
  bool storeFrame(int slotIdx, int bookIdx, const uint8_t* source, size_t bufferSize, size_t freeHeap);

  bool hasValidDiskCache(uint64_t cacheKeyHash, int bookCount, const CacheGeometry& geometry) const;
  bool buildCacheFile(uint64_t cacheKeyHash, int bookCount, const CacheGeometry& geometry, uint8_t* frameBuffer,
                      FrameRenderer renderFrame, void* renderContext, BufferReleaser releaseBuffer,
                      void* releaseContext, bool showProgressPopup, ProgressReporter reportProgress,
                      void* progressContext);
  bool readFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, const CacheGeometry& geometry,
                         uint8_t* dest) const;
  bool loadFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx, const CacheGeometry& geometry,
                         size_t freeHeap);
  int chooseEvictionSlot(int centerIdx, int bookCount, std::optional<int> protectedBookIdx = std::nullopt) const;

  static bool readFrameBytes(HalFile& file, int bookIdx, size_t bufferSize, uint8_t* dest);
  static bool readCacheHeader(HalFile& file, CacheHeader& header);
  static bool isCacheHeaderValid(const CacheHeader& header, uint64_t cacheKeyHash, int bookCount,
                                 const CacheGeometry& geometry);
};

}  // namespace homecarousel
