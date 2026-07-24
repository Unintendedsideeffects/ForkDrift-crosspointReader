#include "HomeCarouselCache.h"

#include <HalStorage.h>
#include <Serialization.h>
#include <SpiBusMutex.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "util/RecentBooksStore.h"

#if defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#endif

namespace homecarousel {
namespace {

// First 8 bytes of the running firmware's ELF SHA-256 as a uint64, or 0 on host
// builds. Any firmware change yields a new value, invalidating cached frames.
uint64_t currentAppFingerprint() {
#if defined(ESP_PLATFORM)
  const esp_app_desc_t* desc = esp_ota_get_app_description();
  if (desc == nullptr) {
    return 0;
  }
  uint64_t fingerprint = 0;
  memcpy(&fingerprint, desc->app_elf_sha256, sizeof(fingerprint));
  return fingerprint;
#else
  return 0;
#endif
}

uint64_t fnvHash64(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (char c : value) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

void appendCoverStateToKey(std::string& key, const RecentBook& book, CoverStateLookup lookup, void* context) {
  key += book.path;
  key += '\0';
  key += book.coverBmpPath;
  key += '\0';

  if (book.coverBmpPath.empty()) {
    key += "0:0";
    key += '\0';
    return;
  }

  bool centerExists = false;
  bool sideExists = false;
  lookup(context, book, centerExists, sideExists);
  key += centerExists ? '1' : '0';
  key += ':';
  key += sideExists ? '1' : '0';
  key += '\0';
}

CacheHeader makeCacheHeader(uint64_t cacheKeyHash, int bookCount, const CacheGeometry& geometry) {
  return {HomeCarouselCache::kMagic,
          HomeCarouselCache::kVersion,
          static_cast<uint16_t>(bookCount),
          geometry.frameBufferSize,
          cacheKeyHash,
          geometry.screenWidth,
          geometry.screenHeight,
          geometry.centerCoverW,
          geometry.centerCoverH,
          geometry.sideCoverW,
          geometry.sideCoverH,
          currentAppFingerprint()};
}

}  // namespace

HomeCarouselCache::~HomeCarouselCache() { invalidate(); }

HomeCarouselCache& HomeCarouselCache::shared() {
  static HomeCarouselCache cache;
  return cache;
}

bool HomeCarouselCache::canAllocateFrameBuffer(const size_t bufferSize, const size_t freeHeap) {
  return freeHeap >= bufferSize + kHeadroom;
}

void HomeCarouselCache::buildCacheKey(const std::vector<RecentBook>& recentBooks, std::string& outKey,
                                      uint64_t& outKeyHash, const CoverStateLookup lookup, void* context,
                                      const std::string& menuSignature) {
  outKey.clear();
  outKey.reserve(512);
  for (const auto& book : recentBooks) {
    appendCoverStateToKey(outKey, book, lookup, context);
  }
  // Fold in the menu layout: frames bake in the icon row, so identical books with
  // a different menu must produce a different key (and thus regenerated frames).
  outKey += '\x1f';  // separator that cannot appear in a cover-state token
  outKey += menuSignature;
  outKeyHash = fnvHash64(outKey);
}

int HomeCarouselCache::findFrameSlot(const int bookIdx) const {
  for (int i = 0; i < kFrameCount; ++i) {
    if (frameBookIdx[i] == bookIdx && frames[i] != nullptr) return i;
  }
  return -1;
}

void HomeCarouselCache::releaseSlot(const int slotIdx) {
  if (slotIdx < 0 || slotIdx >= kFrameCount) return;
  if (frames[slotIdx]) {
    free(frames[slotIdx]);
    frames[slotIdx] = nullptr;
  }
  frameBookIdx[slotIdx] = -1;
}

void HomeCarouselCache::invalidate() {
  for (int i = 0; i < kFrameCount; ++i) releaseSlot(i);
  frameCount = 0;
  lastCenterIdx = -1;
  key.clear();
  keyHash = 0;
}

void HomeCarouselCache::demoteToDiskOnly() {
  for (int i = 0; i < kFrameCount; ++i) releaseSlot(i);
  frameCount = 0;
}

bool HomeCarouselCache::allocateFrameSlots(const int targetFrameCount, const size_t bufferSize, const size_t freeHeap) {
  frameCount = 0;
  if (!canAllocateFrameBuffer(bufferSize, freeHeap)) return false;

  void* probe = malloc(bufferSize);
  if (!probe) return false;
  free(probe);
  frameCount = std::min(targetFrameCount, kFrameCount);
  return frameCount > 0;
}

bool HomeCarouselCache::storeFrame(const int slotIdx, const int bookIdx, const uint8_t* source, const size_t bufferSize,
                                   const size_t freeHeap) {
  if (!source || slotIdx < 0 || slotIdx >= kFrameCount || slotIdx >= frameCount) return false;
  if (!canAllocateFrameBuffer(bufferSize, freeHeap)) return false;

  uint8_t* frame = static_cast<uint8_t*>(malloc(bufferSize));
  if (!frame) {
    demoteToDiskOnly();
    return false;
  }
  releaseSlot(slotIdx);
  memcpy(frame, source, bufferSize);
  frames[slotIdx] = frame;
  frameBookIdx[slotIdx] = bookIdx;
  return true;
}

bool HomeCarouselCache::readCacheHeader(HalFile& file, CacheHeader& header) {
  CacheHeader readHeader{};
  if (!serialization::readPod(file, readHeader)) return false;
  header = readHeader;
  return true;
}

bool HomeCarouselCache::isCacheHeaderValid(const CacheHeader& header, const uint64_t cacheKeyHash, const int bookCount,
                                           const CacheGeometry& geometry) {
  return header.magic == kMagic && header.version == kVersion && header.keyHash == cacheKeyHash &&
         header.frameCount == bookCount && header.frameBufferSize == geometry.frameBufferSize &&
         header.screenWidth == geometry.screenWidth && header.screenHeight == geometry.screenHeight &&
         header.centerCoverW == geometry.centerCoverW && header.centerCoverH == geometry.centerCoverH &&
         header.sideCoverW == geometry.sideCoverW && header.sideCoverH == geometry.sideCoverH &&
         header.appFingerprint == currentAppFingerprint();
}

bool HomeCarouselCache::hasValidDiskCache(const uint64_t cacheKeyHash, const int bookCount,
                                          const CacheGeometry& geometry) const {
  if (bookCount <= 0) return false;

  HalFile cacheFile;
  if (!Storage.openFileForRead("HOME", kCachePath, cacheFile)) return false;
  CacheHeader header{};
  const bool readOk = readCacheHeader(cacheFile, header);
  return readOk && isCacheHeaderValid(header, cacheKeyHash, bookCount, geometry);
}

bool HomeCarouselCache::readFrameBytes(HalFile& file, const int bookIdx, const size_t bufferSize, uint8_t* dest) {
  const size_t frameOffset = sizeof(CacheHeader) + static_cast<size_t>(bookIdx) * bufferSize;
  if (!file.seek(frameOffset)) return false;

  size_t totalRead = 0;
  while (totalRead < bufferSize) {
    const int n = file.read(dest + totalRead, bufferSize - totalRead);
    if (n <= 0) break;
    totalRead += static_cast<size_t>(n);
  }
  return totalRead == bufferSize;
}

bool HomeCarouselCache::buildCacheFile(const uint64_t cacheKeyHash, const int bookCount, const CacheGeometry& geometry,
                                       uint8_t* frameBuffer, const FrameRenderer renderFrame, void* renderContext,
                                       const BufferReleaser releaseBuffer, void* releaseContext,
                                       const bool showProgressPopup, const ProgressReporter reportProgress,
                                       void* progressContext) {
  if (!frameBuffer || bookCount <= 0 || !renderFrame || !releaseBuffer) return false;

  Storage.mkdir("/.crosspoint");
  if (Storage.exists(kCacheTmpPath)) Storage.remove(kCacheTmpPath);

  bool writeFailed = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite("HOME", kCacheTmpPath, file)) return false;

    const CacheHeader header = makeCacheHeader(cacheKeyHash, bookCount, geometry);
    serialization::writePod(file, header);

    if (showProgressPopup && reportProgress) reportProgress(progressContext, 0);

    for (int i = 0; i < bookCount; ++i) {
      const int cachedSlot = findFrameSlot(i);
      if (cachedSlot >= 0 && frames[cachedSlot]) {
        memcpy(frameBuffer, frames[cachedSlot], geometry.frameBufferSize);
      } else {
        for (int slot = 0; slot < kFrameCount; ++slot) releaseSlot(slot);
        renderFrame(renderContext, i);
        releaseBuffer(releaseContext);
      }
      if (file.write(frameBuffer, geometry.frameBufferSize) != geometry.frameBufferSize) {
        writeFailed = true;
        break;
      }
      if (showProgressPopup && reportProgress) reportProgress(progressContext, ((i + 1) * 100) / bookCount);
    }

#ifndef CROSSPOINT_HOST_BUILD
    file.flush();
#endif
  }

  if (writeFailed) {
    Storage.remove(kCacheTmpPath);
    return false;
  }

  if (Storage.exists(kCachePath)) Storage.remove(kCachePath);
  if (!Storage.rename(kCacheTmpPath, kCachePath)) {
    Storage.remove(kCacheTmpPath);
    return false;
  }
  return true;
}

bool HomeCarouselCache::readFrameFromDisk(const uint64_t cacheKeyHash, const int bookCount, const int bookIdx,
                                          const CacheGeometry& geometry, uint8_t* dest) const {
  if (!dest || bookIdx < 0 || bookIdx >= bookCount) return false;

  SpiBusMutex::Guard guard;
  HalFile file;
  if (!Storage.openFileForRead("HOME", kCachePath, file)) return false;

  CacheHeader header{};
  if (!readCacheHeader(file, header) || !isCacheHeaderValid(header, cacheKeyHash, bookCount, geometry)) return false;
  return readFrameBytes(file, bookIdx, geometry.frameBufferSize, dest);
}

bool HomeCarouselCache::loadFrameFromDisk(const uint64_t cacheKeyHash, const int bookCount, const int bookIdx,
                                          const int slotIdx, const CacheGeometry& geometry, const size_t freeHeap) {
  if (slotIdx < 0 || slotIdx >= kFrameCount || bookIdx < 0 || bookIdx >= bookCount || frameCount <= 0) return false;

  if (!frames[slotIdx]) {
    if (!canAllocateFrameBuffer(geometry.frameBufferSize, freeHeap)) return false;
    frames[slotIdx] = static_cast<uint8_t*>(malloc(geometry.frameBufferSize));
    if (!frames[slotIdx]) {
      demoteToDiskOnly();
      return false;
    }
  }
  if (!readFrameFromDisk(cacheKeyHash, bookCount, bookIdx, geometry, frames[slotIdx])) {
    releaseSlot(slotIdx);
    return false;
  }
  frameBookIdx[slotIdx] = bookIdx;
  return true;
}

int HomeCarouselCache::chooseEvictionSlot(const int centerIdx, const int bookCount,
                                          const std::optional<int> protectedBookIdx) const {
  for (int i = 0; i < kFrameCount; ++i) {
    if (frameBookIdx[i] < 0) return i;
  }

  int evictSlot = -1;
  int maxDist = -1;
  for (int i = 0; i < kFrameCount; ++i) {
    if (!frames[i]) continue;
    const int cachedBookIdx = frameBookIdx[i];
    if (protectedBookIdx.has_value() && cachedBookIdx == protectedBookIdx.value()) continue;
    const int diff = std::abs(cachedBookIdx - centerIdx);
    const int dist = std::min(diff, bookCount - diff);
    if (dist > maxDist) {
      maxDist = dist;
      evictSlot = i;
    }
  }
  return evictSlot;
}

}  // namespace homecarousel
