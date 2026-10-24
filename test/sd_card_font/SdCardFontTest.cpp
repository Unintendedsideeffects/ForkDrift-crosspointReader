#include <Arduino.h>
#include <HalStorage.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

MockESP ESP;

namespace {
size_t failArraySize = 0;
int failArrayRemaining = 0;
}  // namespace

void* operator new[](size_t size) {
  void* allocation = std::malloc(size == 0 ? 1 : size);
  if (!allocation) {
    std::fputs("Unexpected host OOM in SdCardFontTest\n", stderr);
    std::exit(EXIT_FAILURE);
  }
  return allocation;
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (failArrayRemaining > 0 && size == failArraySize) {
    --failArrayRemaining;
    return nullptr;
  }
  return std::malloc(size == 0 ? 1 : size);
}

void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }

namespace {
constexpr uint32_t FIRST = 0xAC00;
constexpr uint32_t GLYPHS = 513;
constexpr uint16_t BITMAP_BYTES = 128;
constexpr const char* kPath = "fixture";

std::vector<uint8_t> fontFile;

void put16(size_t at, uint16_t value) {
  fontFile[at] = static_cast<uint8_t>(value);
  fontFile[at + 1] = static_cast<uint8_t>(value >> 8);
}
void put32(size_t at, uint32_t value) {
  put16(at, static_cast<uint16_t>(value));
  put16(at + 2, static_cast<uint16_t>(value >> 16));
}

bool publish() {
  Storage.reset();
  ESP.reset();
  failArraySize = 0;
  failArrayRemaining = 0;
  HalFile file;
  if (!Storage.openFileForWrite("SDCF", kPath, file)) return false;
  return file.write(fontFile.data(), fontFile.size()) == fontFile.size();
}

void makeFont(uint32_t glyphs = GLYPHS) {
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  const size_t BITMAP_OFFSET = GLYPH_OFFSET + glyphs * sizeof(EpdGlyph);
  fontFile.assign(BITMAP_OFFSET + glyphs * BITMAP_BYTES, 0);
  std::memcpy(fontFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  fontFile[12] = 1;
  put32(36, 2);
  put32(40, glyphs);
  fontFile[44] = 32;
  put16(45, 32);
  put32(56, 64);
  put32(64, FIRST);
  put32(68, FIRST + glyphs - 2);
  put32(76, 0xFFFD);
  put32(80, 0xFFFD);
  put32(84, glyphs - 1);
  for (uint32_t i = 0; i < glyphs; ++i) {
    EpdGlyph glyph{};
    glyph.width = 32;
    glyph.height = 32;
    glyph.advanceX = 32 << 4;
    glyph.top = 32;
    glyph.dataLength = BITMAP_BYTES;
    glyph.dataOffset = (glyphs - 1 - i) * BITMAP_BYTES;
    std::memcpy(fontFile.data() + GLYPH_OFFSET + i * sizeof(glyph), &glyph, sizeof(glyph));
    std::memset(fontFile.data() + BITMAP_OFFSET + glyph.dataOffset, static_cast<int>(i % 251), BITMAP_BYTES);
  }
}

void makeKerningFont(uint16_t extraEntries = 0, uint8_t classCount = 2) {
  constexpr uint32_t KERN_GLYPHS = 7;
  constexpr size_t GLYPH_OFFSET = 64 + 24;
  constexpr size_t KERN_OFFSET = GLYPH_OFFSET + KERN_GLYPHS * sizeof(EpdGlyph);
  constexpr uint8_t LEFT[][2] = {{'A', 1}, {'C', 2}};
  constexpr uint8_t RIGHT[][2] = {{'B', 1}, {'D', 2}};
  constexpr int8_t MATRIX[2][2] = {{-3, 0}, {4, -5}};
  const uint16_t entries = classCount ? static_cast<uint16_t>(2 + extraEntries) : 0;
  const size_t matrixBytes = static_cast<size_t>(classCount) * classCount;
  const size_t bitmapOffset = KERN_OFFSET + entries * 3 * 2 + matrixBytes;
  fontFile.assign(bitmapOffset + KERN_GLYPHS * BITMAP_BYTES, 0);
  std::memcpy(fontFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  fontFile[12] = 1;
  put32(36, 2);
  put32(40, KERN_GLYPHS);
  fontFile[44] = 32;
  put16(45, 32);
  put16(49, entries);
  put16(51, entries);
  fontFile[53] = classCount;
  fontFile[54] = classCount;
  put32(56, 64);
  put32(64, 'A');
  put32(68, 'F');
  put32(76, 0xFFFD);
  put32(80, 0xFFFD);
  put32(84, KERN_GLYPHS - 1);
  for (uint32_t i = 0; i < KERN_GLYPHS; ++i) {
    EpdGlyph glyph{};
    glyph.width = 32;
    glyph.height = 32;
    glyph.advanceX = 32 << 4;
    glyph.top = 32;
    glyph.dataLength = BITMAP_BYTES;
    glyph.dataOffset = i * BITMAP_BYTES;
    std::memcpy(fontFile.data() + GLYPH_OFFSET + i * sizeof(glyph), &glyph, sizeof(glyph));
    std::memset(fontFile.data() + bitmapOffset + glyph.dataOffset, static_cast<int>(i % 251), BITMAP_BYTES);
  }
  size_t at = KERN_OFFSET;
  for (const auto* table : {LEFT, RIGHT}) {
    if (classCount == 0) break;
    for (size_t i = 0; i < 2; ++i, at += 3) {
      put16(at, table[i][0]);
      fontFile[at + 2] = table[i][1];
    }
    for (uint16_t i = 0; i < extraEntries; ++i, at += 3) {
      put16(at, static_cast<uint16_t>(0x100 + i));
      fontFile[at + 2] = 2;
    }
  }
  for (size_t row = 0; classCount > 0 && row < 2; ++row) {
    std::memcpy(fontFile.data() + at + row * classCount, MATRIX[row], sizeof(MATRIX[row]));
  }
}

std::string page(uint32_t first, uint32_t count) {
  std::string text;
  text.reserve(count * 3);
  for (uint32_t cp = first; cp < first + count; ++cp) {
    text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

void expectPageBitmaps(SdCardFont& font, uint32_t first, uint32_t count) {
  const auto* data = font.getEpdFont()->data;
  ASSERT_NE(nullptr, data);
  ASSERT_NE(nullptr, data->bitmap);
  for (uint32_t cp = first; cp < first + count; ++cp) {
    const EpdGlyph* glyph = nullptr;
    for (uint32_t i = 0; i < data->intervalCount; ++i) {
      const auto& interval = data->intervals[i];
      if (cp >= interval.first && cp <= interval.last) glyph = data->glyph + interval.offset + cp - interval.first;
    }
    ASSERT_NE(nullptr, glyph) << cp;
    ASSERT_EQ(BITMAP_BYTES, glyph->dataLength);
    for (uint16_t i = 0; i < BITMAP_BYTES; ++i) {
      ASSERT_EQ((cp - FIRST) % 251, data->bitmap[glyph->dataOffset + i]);
    }
  }
}
}  // namespace

TEST(SdCardFontTest, MappingAndReadOrderRaiiFreesOnErrorReturns) {
  makeFont();
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  Storage.failNextReads(1);
  const auto text = page(FIRST, 20);
  EXPECT_EQ(21, font.prewarm(text.c_str(), 1));
  EXPECT_EQ(nullptr, font.getEpdFont()->data->bitmap);
}

TEST(SdCardFontTest, BitmapAllocationRetriesAfterEvictingRebuildableAdvances) {
  makeFont();
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  const auto text = page(FIRST, 200);
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  ASSERT_TRUE(font.hasAdvanceTable());
  struct Guard {
    ~Guard() {
      ESP.reset();
      failArraySize = 0;
      failArrayRemaining = 0;
    }
  } guard;
  ESP.overrideMaxAllocHeap = 8 * 1024;
  failArraySize = 201 * BITMAP_BYTES;
  failArrayRemaining = 1;
  ASSERT_EQ(0, font.prewarm(text.c_str(), 1));
  EXPECT_EQ(0, failArrayRemaining);
  EXPECT_FALSE(font.hasAdvanceTable());
  expectPageBitmaps(font, FIRST, 200);
}

TEST(SdCardFontTest, BitmapRetryStillFailingLeavesMiniCacheEmpty) {
  makeFont();
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  const auto text = page(FIRST, 200);
  ASSERT_EQ(0, font.buildAdvanceTable(text.c_str(), 1));
  ASSERT_TRUE(font.hasAdvanceTable());
  struct Guard {
    ~Guard() {
      failArraySize = 0;
      failArrayRemaining = 0;
    }
  } guard;
  failArraySize = 201 * BITMAP_BYTES;
  failArrayRemaining = 2;
  EXPECT_EQ(201, font.prewarm(text.c_str(), 1));
  EXPECT_EQ(0, failArrayRemaining);
  EXPECT_FALSE(font.hasAdvanceTable());
  EXPECT_EQ(nullptr, font.getEpdFont()->data->bitmap);
  EXPECT_EQ(0U, font.getEpdFont()->data->intervalCount);
}

TEST(SdCardFontTest, AdvancesStayCorrectWhenPagesAddCodepointsOutOfOrder) {
  makeFont();
  for (uint32_t i = 0; i < GLYPHS; ++i) {
    put16(64 + 24 + i * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (20 + i % 13) << 4);
  }
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  std::vector<uint32_t> added;
  for (uint32_t block : {3U, 0U, 5U, 1U, 4U, 2U}) {
    ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST + block * 80, 80).c_str(), 1));
    for (uint32_t cp = FIRST + block * 80; cp < FIRST + block * 80 + 80; ++cp) added.push_back(cp);
    for (uint32_t cp : added) {
      ASSERT_EQ((20 + (cp - FIRST) % 13) << 4, font.getAdvance(cp, 0)) << std::hex << cp;
    }
  }
}

TEST(SdCardFontTest, AdvanceMergesPastTheCapKeepTheLowestCodepoints) {
  constexpr uint32_t CACHE_LIMIT = 768;
  constexpr uint32_t GLYPH_COUNT = 1001;
  makeFont(GLYPH_COUNT);
  for (uint32_t i = 0; i < GLYPH_COUNT; ++i) {
    put16(64 + 24 + i * sizeof(EpdGlyph) + offsetof(EpdGlyph, advanceX), (20 + i % 13) << 4);
  }
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  std::vector<uint32_t> expected;
  for (uint32_t block : {6U, 1U, 9U, 3U, 0U, 7U, 4U, 8U, 2U, 5U}) {
    ASSERT_GE(font.buildAdvanceTable(page(FIRST + block * 100, 100).c_str(), 1), 0);
    if (expected.size() < CACHE_LIMIT) {
      for (uint32_t cp = FIRST + block * 100; cp < FIRST + block * 100 + 100; ++cp) expected.push_back(cp);
      std::sort(expected.begin(), expected.end());
      if (expected.size() > CACHE_LIMIT) expected.resize(CACHE_LIMIT);
    }
    for (uint32_t cp = FIRST; cp < FIRST + 1000; ++cp) {
      const bool cached = std::binary_search(expected.begin(), expected.end(), cp);
      ASSERT_EQ(cached ? (20 + (cp - FIRST) % 13) << 4 : 0, font.getAdvance(cp, 0)) << block << " " << std::hex << cp;
    }
  }
}

TEST(SdCardFontTest, AnEmptyOrFailedAdvanceBuildLeavesNoTable) {
  makeFont();
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  font.buildAdvanceTable("", 1);
  EXPECT_FALSE(font.hasAdvanceTable());
  failArraySize = 122 * sizeof(uint32_t);
  failArrayRemaining = 1;
  EXPECT_EQ(-1, font.buildAdvanceTable(page(FIRST, 40).c_str(), 1));
  EXPECT_FALSE(font.hasAdvanceTable());
  failArraySize = 0;
  failArrayRemaining = 0;
  ASSERT_EQ(0, font.buildAdvanceTable(page(FIRST, 10).c_str(), 1));
  EXPECT_TRUE(font.hasAdvanceTable());
}

TEST(SdCardFontTest, PagesKernAndAWarmIndexReadsFewerBlocks) {
  makeKerningFont(300);
  ASSERT_TRUE(publish());
  SdCardFont font;
  ASSERT_TRUE(font.load(kPath));
  gMockHalReadCalls = 0;
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1));
  const int firstReads = Storage.readCallCount();
  const EpdFont* epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(0, epd->getKerning('A', 'D'));
  EXPECT_EQ(4, epd->getKerning('C', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));

  font.clearCache();
  gMockHalReadCalls = 0;
  ASSERT_EQ(0, font.prewarm("ABCDEF", 1));
  EXPECT_EQ(firstReads - 8, Storage.readCallCount());
  epd = font.getEpdFont();
  EXPECT_EQ(-3, epd->getKerning('A', 'B'));
  EXPECT_EQ(-5, epd->getKerning('C', 'D'));
}
