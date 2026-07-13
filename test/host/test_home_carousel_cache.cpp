#include <algorithm>
#include <cstdint>
#include <vector>

#include "activities/home/HomeCarouselCache.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {

homecarousel::CacheGeometry testGeometry(const size_t frameSize) {
  homecarousel::CacheGeometry geometry;
  geometry.frameBufferSize = static_cast<uint32_t>(frameSize);
  geometry.screenWidth = 800;
  geometry.screenHeight = 480;
  geometry.centerCoverW = 100;
  geometry.centerCoverH = 150;
  geometry.sideCoverW = 60;
  geometry.sideCoverH = 90;
  return geometry;
}

struct RenderContext {
  std::vector<uint8_t>* frame = nullptr;
};

void renderFrame(void* context, const int bookIdx) {
  auto* render = static_cast<RenderContext*>(context);
  std::fill(render->frame->begin(), render->frame->end(), static_cast<uint8_t>(0x10 + bookIdx));
}

void releaseBuffer(void*) {}

}  // namespace

TEST_CASE("HomeCarouselCache cache file round-trips rendered frames byte-for-byte") {
  Storage.reset();
  constexpr size_t frameSize = 16;
  const auto geometry = testGeometry(frameSize);
  std::vector<uint8_t> frameBuffer(frameSize);
  RenderContext renderContext{&frameBuffer};
  homecarousel::HomeCarouselCache cache;

  REQUIRE(cache.allocateFrameSlots(1, frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  const std::vector<uint8_t> cachedFrame(frameSize, 0x11);
  REQUIRE(
      cache.storeFrame(0, 1, cachedFrame.data(), frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  REQUIRE(cache.buildCacheFile(0x1234, 3, geometry, frameBuffer.data(), &renderFrame, &renderContext, &releaseBuffer,
                               nullptr, false, nullptr, nullptr));

  for (int bookIdx = 0; bookIdx < 3; ++bookIdx) {
    std::vector<uint8_t> loaded(frameSize, 0);
    REQUIRE(cache.readFrameFromDisk(0x1234, 3, bookIdx, geometry, loaded.data()));
    CHECK(loaded == std::vector<uint8_t>(frameSize, static_cast<uint8_t>(0x10 + bookIdx)));
  }

  cache.invalidate();
  REQUIRE(cache.allocateFrameSlots(1, frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  REQUIRE(cache.loadFrameFromDisk(0x1234, 3, 2, 0, geometry, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  CHECK(cache.findFrameSlot(2) == 0);
  CHECK(std::vector<uint8_t>(cache.frames[0], cache.frames[0] + frameSize) == std::vector<uint8_t>(frameSize, 0x12));
}

TEST_CASE("HomeCarouselCache tags slots and preserves protected eviction behavior") {
  constexpr size_t frameSize = 8;
  const std::vector<uint8_t> frame(frameSize, 0x2a);
  homecarousel::HomeCarouselCache cache;

  CHECK(cache.chooseEvictionSlot(2, 5) == 0);
  REQUIRE(cache.allocateFrameSlots(1, frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  REQUIRE(cache.storeFrame(0, 4, frame.data(), frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  CHECK(cache.findFrameSlot(4) == 0);
  CHECK(cache.chooseEvictionSlot(2, 5) == 0);
  CHECK(cache.chooseEvictionSlot(2, 5, 4) == -1);

  cache.releaseSlot(0);
  CHECK(cache.findFrameSlot(4) == -1);
  CHECK(cache.chooseEvictionSlot(2, 5) == 0);
}

TEST_CASE("HomeCarouselCache allocation policy keeps the exact headroom boundary") {
  constexpr size_t frameSize = 4096;
  CHECK(homecarousel::HomeCarouselCache::canAllocateFrameBuffer(
      frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom));
  CHECK_FALSE(homecarousel::HomeCarouselCache::canAllocateFrameBuffer(
      frameSize, frameSize + homecarousel::HomeCarouselCache::kHeadroom - 1));
  CHECK(homecarousel::HomeCarouselCache::canAllocateFrameBuffer(0, homecarousel::HomeCarouselCache::kHeadroom));
}
