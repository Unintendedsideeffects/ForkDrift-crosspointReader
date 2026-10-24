#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "HalStorage.h"
#include "activities/reader/LinkStack.h"
#include "doctest/doctest.h"

namespace {

LinkStack stackFrom(const std::vector<LinkPosition>& items) {
  LinkStack stack;
  for (const auto& item : items) {
    stack.push(item.spineIndex, item.pageNumber);
  }
  return stack;
}

}  // namespace

TEST_CASE("LinkStack round-trips depth 1, 2, and 3") {
  for (int depth = 1; depth <= LinkStack::kMaxDepth; depth++) {
    LinkStack stack;
    for (int i = 0; i < depth; i++) {
      stack.push(i + 1, (i + 1) * 10);
    }
    uint8_t data[LinkStack::kMaxBytes];
    const size_t size = stack.encode(data, sizeof(data));
    REQUIRE(size == 2 + static_cast<size_t>(depth) * 4);
    CHECK(data[0] == LinkStack::kFileVersion);
    CHECK(data[1] == depth);

    LinkStack decoded;
    REQUIRE(LinkStack::decode(data, size, 16, decoded));
    CHECK(decoded.depth == depth);
    for (int i = 0; i < depth; i++) {
      CHECK(decoded.entries[i].spineIndex == i + 1);
      CHECK(decoded.entries[i].pageNumber == (i + 1) * 10);
    }
  }
}

TEST_CASE("LinkStack drop-oldest at depth 3 keeps the newest jumps") {
  LinkStack stack = stackFrom({{1, 10}, {2, 20}, {3, 30}, {4, 40}});
  CHECK(stack.depth == 3);
  CHECK(stack.entries[0].spineIndex == 2);
  CHECK(stack.entries[1].spineIndex == 3);
  CHECK(stack.entries[2].spineIndex == 4);
  CHECK(stack.entries[2].pageNumber == 40);
}

TEST_CASE("LinkStack decode rejects corrupt payloads") {
  LinkStack stack = stackFrom({{1, 2}});
  uint8_t data[LinkStack::kMaxBytes];
  const size_t size = stack.encode(data, sizeof(data));
  REQUIRE(size > 0);

  LinkStack decoded;
  CHECK_FALSE(LinkStack::decode(data, 0, 8, decoded));
  CHECK(decoded.depth == 0);

  uint8_t depthZero[2] = {LinkStack::kFileVersion, 0};
  CHECK_FALSE(LinkStack::decode(depthZero, sizeof(depthZero), 8, decoded));

  uint8_t depthFour[2] = {LinkStack::kFileVersion, 4};
  CHECK_FALSE(LinkStack::decode(depthFour, sizeof(depthFour), 8, decoded));

  CHECK_FALSE(LinkStack::decode(data, size - 1, 8, decoded));
  CHECK_FALSE(LinkStack::decode(data, size + 1, 8, decoded));

  uint8_t badVersion[LinkStack::kMaxBytes];
  std::memcpy(badVersion, data, size);
  badVersion[0] = 99;
  CHECK_FALSE(LinkStack::decode(badVersion, size, 8, decoded));

  CHECK_FALSE(LinkStack::decode(data, size, 1, decoded));
  CHECK(decoded.depth == 0);
}

TEST_CASE("loadLinkStackFile consumes the sidecar once") {
  Storage.reset();
  Storage.mkdir("/book");
  LinkStack stack = stackFrom({{2, 7}, {3, 1}});
  REQUIRE(saveLinkStackFile("/book", stack));
  CHECK(Storage.exists("/book/links.bin"));

  LinkStack loaded;
  REQUIRE(loadLinkStackFile("/book", 10, loaded));
  CHECK(loaded.depth == 2);
  CHECK(loaded.entries[0].spineIndex == 2);
  CHECK(loaded.entries[1].pageNumber == 1);
  CHECK_FALSE(Storage.exists("/book/links.bin"));

  LinkStack second;
  CHECK_FALSE(loadLinkStackFile("/book", 10, second));
  CHECK(second.depth == 0);
}

TEST_CASE("loadLinkStackFile rejects oversized and missing files") {
  Storage.reset();
  Storage.mkdir("/book");
  LinkStack loaded;
  CHECK_FALSE(loadLinkStackFile("/book", 10, loaded));

  HalFile f;
  REQUIRE(Storage.openFileForWrite("TEST", "/book/links.bin", f));
  uint8_t oversized[LinkStack::kMaxBytes + 1] = {};
  oversized[0] = LinkStack::kFileVersion;
  oversized[1] = 3;
  REQUIRE(f.write(oversized, sizeof(oversized)) == sizeof(oversized));
  CHECK_FALSE(loadLinkStackFile("/book", 10, loaded));
  CHECK_FALSE(Storage.exists("/book/links.bin"));
}

TEST_CASE("bookmark override discards sidecar without applying it") {
  Storage.reset();
  Storage.mkdir("/book");
  LinkStack stack = stackFrom({{1, 4}});
  REQUIRE(saveLinkStackFile("/book", stack));
  discardLinkStackFile("/book");
  CHECK_FALSE(Storage.exists("/book/links.bin"));
  LinkStack loaded;
  CHECK_FALSE(loadLinkStackFile("/book", 10, loaded));
  CHECK(loaded.depth == 0);
}
