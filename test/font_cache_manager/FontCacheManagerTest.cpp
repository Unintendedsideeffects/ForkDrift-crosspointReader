#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>

#include "FontCacheManager.h"
#include "FontDecompressor.h"
#include "SdCardFont.h"

namespace {

bool countHeapAllocations = false;
size_t heapAllocationCount = 0;

const SdCardFont::PrewarmCall* findCall(const SdCardFont& font, const uint8_t styleMask) {
  for (int i = 0; i < font.prewarmCallCount; i++) {
    if (font.prewarmCalls[i].styleMask == styleMask) return &font.prewarmCalls[i];
  }
  return nullptr;
}

std::string utf8Range(uint32_t first, uint32_t count) {
  std::string text;
  text.reserve(count * 2);
  for (uint32_t cp = first; cp < first + count; ++cp) {
    text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    text.push_back(static_cast<char>(0x80 | (cp & 63)));
  }
  return text;
}

}  // namespace

void* operator new(const size_t size) {
  if (countHeapAllocations) heapAllocationCount++;
  if (void* allocation = std::malloc(size)) return allocation;
  throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete(void* allocation, size_t) noexcept { std::free(allocation); }

TEST(FontCacheManagerTest, PrewarmScopeBatchesEachFontAndResolvedStyleSeparately) {
  SdCardFont readerFont;
  SdCardFont fallbackFont;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{-17, &readerFont}, {23, &fallbackFont}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", -17, EpdFontFamily::REGULAR);
  manager.recordText("B", -17, EpdFontFamily::BOLD);
  manager.recordText("C", 23, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2, readerFont.prewarmCallCount);
  const auto* regularCall = findCall(readerFont, 0x01);
  ASSERT_NE(nullptr, regularCall);
  EXPECT_STREQ("A", regularCall->text);
  const auto* boldCall = findCall(readerFont, 0x02);
  ASSERT_NE(nullptr, boldCall);
  EXPECT_STREQ("B", boldCall->text);

  ASSERT_EQ(1, fallbackFont.prewarmCallCount);
  EXPECT_STREQ("C", fallbackFont.prewarmCalls[0].text);
  EXPECT_EQ(0x01, fallbackFont.prewarmCalls[0].styleMask);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesStylesThatResolveToTheSameSdFontData) {
  SdCardFont font;
  font.resolvedStyles[EpdFontFamily::BOLD] = EpdFontFamily::REGULAR;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 7, EpdFontFamily::REGULAR);
  manager.recordText("B", 7, EpdFontFamily::BOLD);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_STREQ("AB", font.prewarmCalls[0].text);
  EXPECT_EQ(0x01, font.prewarmCalls[0].styleMask);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesBuiltInStylesThatShareFontData) {
  EpdFontData regularData{reinterpret_cast<const void*>(1)};
  EpdFontData boldData{reinterpret_cast<const void*>(1)};
  const EpdFontFamily family(&regularData, &boldData, &regularData, nullptr);
  const std::map<int, EpdFontFamily> builtinFonts{{5, family}};
  const std::map<int, SdCardFont*> noSdFonts;
  FontDecompressor decompressor;
  FontCacheManager manager(builtinFonts, noSdFonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 5, EpdFontFamily::REGULAR);
  manager.recordText("B", 5, EpdFontFamily::BOLD);
  manager.recordText("C", 5, EpdFontFamily::ITALIC);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2, decompressor.prewarmCallCount);
  const auto& first = decompressor.prewarmCalls[0];
  const auto& second = decompressor.prewarmCalls[1];
  const auto* regularCall = first.fontData == &regularData ? &first : &second;
  const auto* boldCall = first.fontData == &boldData ? &first : &second;
  EXPECT_STREQ("AC", regularCall->text);
  EXPECT_STREQ("B", boldCall->text);
}

TEST(FontCacheManagerTest, PrewarmScopePreservesUniqueMultibyteCodepoints) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80\xC3\xA9", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_STREQ("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", font.prewarmCalls[0].text);
}

TEST(FontCacheManagerTest, PrewarmScanDoesNotAllocateHeapMemory) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  heapAllocationCount = 0;
  countHeapAllocations = true;
  auto scope = manager.createPrewarmScope();
  manager.recordText("Repeated text: \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();
  countHeapAllocations = false;

  EXPECT_EQ(0U, heapAllocationCount);
}

TEST(FontCacheManagerTest, EmptyScanIsANoOp) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  EXPECT_EQ(0, font.prewarmCallCount);
}

TEST(FontCacheManagerTest, ScanDropsThe513thUniqueCodepoint) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  const auto text = utf8Range(0x100, 513);
  auto scope = manager.createPrewarmScope();
  manager.recordText(text.c_str(), 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_EQ(1024U, std::strlen(font.prewarmCalls[0].text));
  EXPECT_EQ(nullptr, std::strstr(font.prewarmCalls[0].text, "\xCC\x80"));
}

TEST(FontCacheManagerTest, TwoFontsTimesTwoStylesStayInsideScanFontCap) {
  SdCardFont fontA;
  SdCardFont fontB;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{1, &fontA}, {2, &fontB}};
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 1, EpdFontFamily::REGULAR);
  manager.recordText("B", 1, EpdFontFamily::BOLD);
  manager.recordText("C", 2, EpdFontFamily::REGULAR);
  manager.recordText("D", 2, EpdFontFamily::ITALIC);
  scope.endScanAndPrewarm();

  EXPECT_EQ(2, fontA.prewarmCallCount);
  EXPECT_EQ(2, fontB.prewarmCallCount);
  EXPECT_NE(nullptr, findCall(fontA, 0x01));
  EXPECT_NE(nullptr, findCall(fontA, 0x02));
  EXPECT_NE(nullptr, findCall(fontB, 0x01));
  EXPECT_NE(nullptr, findCall(fontB, 0x04));
}
