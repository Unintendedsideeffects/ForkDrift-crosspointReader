#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "EpdFont.h"
#define private public
#include "SdCardFont.h"
#undef private

// Intercept arrays only, with no allocating bookkeeping, to inject each resident-table OOM
// and verify owner/alias cleanup. Both new[] variants and deletes use the same backing allocator.
namespace {
struct Allocation {
  void* ptr;
  size_t bytes;
};
Allocation arrays[128]{};
int failArrayCall = -1, arrayCalls = 0;
void* allocateArray(size_t n) {
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  for (auto& a : arrays)
    if (!a.ptr) {
      a = {p, n};
      return p;
    }
  std::abort();
}
size_t arrayBytes() {
  size_t bytes = 0;
  for (const auto& a : arrays)
    if (a.ptr) bytes += a.bytes;
  return bytes;
}
void freeArray(void* p) {
  if (!p) return;
  for (auto& a : arrays)
    if (a.ptr == p) {
      a = {};
      std::free(p);
      return;
    }
  std::abort();
}
}  // namespace
void* operator new[](size_t n) { return allocateArray(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept {
  if (arrayCalls++ == failArrayCall) return nullptr;
  return allocateArray(n);
}
void operator delete[](void* p) noexcept { freeArray(p); }
void operator delete[](void* p, size_t) noexcept { freeArray(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { freeArray(p); }

namespace {
using Table = std::vector<EpdUnicodeInterval>;
struct Style {
  uint8_t id;
  Table intervals;
  uint32_t extraGlyphs = 0;
};
uint32_t glyphCount(const Table& t) {
  uint32_t count = 0;
  for (const auto& iv : t) count += iv.last - iv.first + 1;
  return count;
}
Table table(std::initializer_list<std::pair<uint32_t, uint32_t>> ranges) {
  Table out;
  uint32_t offset = 0;
  for (auto r : ranges) {
    out.push_back({r.first, r.second, offset});
    offset += r.second - r.first + 1;
  }
  return out;
}
uint16_t advance(uint32_t index, uint8_t style) {
  return static_cast<uint16_t>((index * 17 + 9 + style * 100) % 65535 + 1);
}
void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) bytes[at + i] = n >> (8 * i);
}
void append(std::vector<uint8_t>& bytes, const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  bytes.insert(bytes.end(), p, p + n);
}
void fixture(const std::string& path, const std::vector<Style>& styles) {
  std::vector<uint8_t> bytes(32 + styles.size() * 32, 0);
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  bytes[8] = CPFONT_VERSION;
  bytes[12] = styles.size();
  for (size_t i = 0; i < styles.size(); ++i) {
    const auto& s = styles[i];
    size_t toc = 32 + i * 32;
    bytes[toc] = s.id;
    put32(bytes, toc + 4, s.intervals.size());
    uint32_t count = glyphCount(s.intervals) + s.extraGlyphs;
    put32(bytes, toc + 8, count);
    bytes[toc + 12] = 20;
    put32(bytes, toc + 24, bytes.size());
    append(bytes, s.intervals.data(), s.intervals.size() * sizeof(EpdUnicodeInterval));
    for (uint32_t g = 0; g < count; ++g) {
      EpdGlyph glyph{};
      glyph.advanceX = advance(g, s.id);
      glyph.width = glyph.height = 1;
      glyph.dataLength = 1;
      append(bytes, &glyph, sizeof(glyph));
    }
    bytes.push_back(0x80);
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
int32_t reference(const Table& intervals, uint32_t cp) {
  for (const auto& iv : intervals)
    if (cp >= iv.first && cp <= iv.last) return iv.offset + cp - iv.first;
  return -1;
}
void verify(SdCardFont& font, uint8_t id, const Table& intervals, uint32_t compact) {
  const auto& s = font.styles_[id];
  assert(s.bmpIntervalCount == compact);
  assert(bool(s.bmpIntervals) == (compact != 0));
  assert(bool(s.fullIntervals) == (compact != intervals.size()));
  // Exhaustively compare all Unicode codepoints, including misses on both sides of the split.
  for (uint32_t cp = 0; cp <= 0x110000; ++cp) {
    const auto expected = reference(intervals, cp);
    assert(font.findGlobalGlyphIndex(s, cp) == expected);
    assert(font.getEpdFont(id)->hasCodepoint(cp) == (expected >= 0));
  }
  for (const auto& iv : intervals)
    for (uint32_t cp : {iv.first, iv.last}) {
      uint16_t actual = 0;
      assert(font.readAdvance(cp, id, &actual));
      assert(actual == advance(reference(intervals, cp), id));
    }
  assert(font.findGlobalGlyphIndex(s, UINT32_MAX) == -1);
}
void clean(const SdCardFont& f) {
  assert(!f.loaded_ && f.styleCount() == 0 && f.contentHash() == 0);
  for (const auto& s : f.styles_) {
    assert(!s.present && !s.fullIntervals && !s.bmpIntervals && !s.intervalsShared && s.bmpIntervalCount == 0);
  }
  assert(arrayBytes() == 0 && openFiles == 0);
}
}  // namespace

int main() {
  auto dir = std::filesystem::temp_directory_path() / "crossink-font-interval-tests";
  std::filesystem::create_directories(dir);
  const auto path = (dir / "test.cpfont").string();
  const auto bmp = table({{0, 2}, {0x41, 0x44}, {0xFFFD, 0xFFFF}});
  const auto mixed =
      table({{0x41, 0x44}, {0x4E00, 0x4E02}, {0xFFFD, 0xFFFF}, {0x10000, 0x10002}, {0x10FFFF, 0x10FFFF}});
  const auto crossing = table({{0x41, 0x42}, {0xFFFE, 0x10002}, {0x20000, 0x20000}});
  const auto supplementary = table({{0x10000, 0x10001}, {0x10FFFF, 0x10FFFF}});
  const auto fullBmp = table({{0, 0xFFFE}, {0xFFFF, 0xFFFF}});  // offset 65535; total glyphs 65536
  const auto maxGlyphsMixed = table({{0, 0xFFFE}, {0x10000, 0x10000}});
  for (auto item :
       {std::pair{bmp, 3u}, {mixed, 3u}, {crossing, 1u}, {supplementary, 0u}, {fullBmp, 2u}, {maxGlyphsMixed, 1u}}) {
    fixture(path, {{0, item.first}});
    SdCardFont font;
    assert(font.load(path.c_str()));
    verify(font, 0, item.first, item.second);
    size_t expected = item.second * 6 + (item.first.size() - item.second) * 12;
    assert(arrayBytes() == expected);
    font.clearCache();
    font.releaseForLowMemory();
    assert(arrayBytes() == expected);
    verify(font, 0, item.first, item.second);
    font.freeAll();
    clean(font);
  }

  // Maximum interval count: sparse BMP prefix plus one supplementary singleton.
  Table sparse;
  sparse.reserve(4096);
  for (uint32_t i = 0; i < 4095; ++i) sparse.push_back({i * 2, i * 2, i});
  sparse.push_back({0x20000, 0x20000, 4095});
  fixture(path, {{0, sparse}, {1, sparse}, {2, sparse}, {3, sparse}});
  {
    SdCardFont font;
    assert(font.load(path.c_str()));
    assert(font.styles_[0].bmpIntervalCount == 4095);
    assert(arrayBytes() == 4095 * 6 + 12);
    for (uint8_t id = 0; id < 4; ++id) {
      if (id) assert(font.styles_[id].intervalsShared);
      for (uint32_t cp = 0; cp < 8192; ++cp)
        assert(font.findGlobalGlyphIndex(font.styles_[id], cp) == reference(sparse, cp));
      assert(font.findGlobalGlyphIndex(font.styles_[id], 0x20000) == 4095);
    }
    std::printf("Sparse fixture: 1 shared table: old=49152 B new=%zu B saved=24570 B (table payload only)\n",
                arrayBytes());
  }
  assert(arrayBytes() == 0);

  // Four styles: two share mixed coverage, one differs in the full suffix, one in the compact prefix.
  auto differentFull = mixed;
  differentFull.back().first = differentFull.back().last = 0x10FFFE;
  auto differentBmp = mixed;
  differentBmp.front().first++;
  differentBmp.front().last++;
  fixture(path, {{3, differentBmp}, {1, mixed, 1}, {2, differentFull}, {0, mixed}});  // out-of-order TOC
  {
    SdCardFont font;
    assert(font.load(path.c_str()));
    assert(font.styles_[1].intervalsShared);
    assert(font.styles_[1].bmpIntervals == font.styles_[0].bmpIntervals);
    assert(font.styles_[1].fullIntervals == font.styles_[0].fullIntervals);
    assert(!font.styles_[2].intervalsShared && !font.styles_[3].intervalsShared);
    verify(font, 0, mixed, 3);
    verify(font, 1, mixed, 3);
    verify(font, 2, differentFull, 3);
    verify(font, 3, differentBmp, 3);
    assert(arrayBytes() == 3 * (3 * 6 + 2 * 12));
    std::printf(
        "Mixed fixture: 3 distinct tables x (3 BMP + 2 full): old=180 B new=%zu B saved=54 B (table payload only)\n",
        arrayBytes());
    assert(font.prewarm("A\xF0\x90\x80\x80", 0x03, true) == 0);
    assert(font.getEpdFont(0)->getGlyph(0x10000)->advanceX == advance(reference(mixed, 0x10000), 0));
    const uint32_t cps[] = {0x41, 0x10000};
    assert(font.buildAdvanceTableForCodepoints(cps, 2, false, false, 0x03) == 0);
    assert(font.getAdvance(0x10000, 1) == advance(reference(mixed, 0x10000), 1));
    font.freeAll();
    clean(font);
  }
  // Fail every split-table allocation, including after an alias and during a later distinct table.
  for (int failure = 0; failure < 6; ++failure) {
    SdCardFont font;
    assert(font.load(path.c_str()));  // A failed replacement load must also release the previous font.
    arrayCalls = 0;
    failArrayCall = failure;
    assert(!font.load(path.c_str()));
    failArrayCall = -1;
    clean(font);
    assert(font.load(path.c_str()));
    font.freeAll();
    clean(font);
  }
  // Fail every read in load (header, TOC, validation, compact conversion, full suffix) and retry.
  readCalls = 0;
  {
    SdCardFont font;
    assert(font.load(path.c_str()));
  }
  int loadReads = readCalls;
  for (int failure = 0; failure < loadReads; ++failure) {
    SdCardFont font;
    assert(font.load(path.c_str()));
    readCalls = 0;
    failReadCall = failure;
    assert(!font.load(path.c_str()));
    failReadCall = -1;
    clean(font);
    assert(font.load(path.c_str()));
    font.freeAll();
    clean(font);
  }
  // Malformed later style after an earlier mixed table is shared must free its single owner.
  auto invalid = mixed;
  invalid.back().offset++;
  fixture(path, {{0, mixed}, {1, mixed}, {2, invalid}});
  {
    SdCardFont font;
    assert(!font.load(path.c_str()));
    clean(font);
    fixture(path, {{0, bmp}});
    assert(font.load(path.c_str()));
  }
  assert(arrayBytes() == 0 && openFiles == 0);
  for (auto invalidTable : {table({{0, 0x10000}}), table({{0x41, 0x41}})}) {
    // Reject glyph counts above 65536 and offsets above 65535 rather than narrowing.
    if (invalidTable[0].last == 0x41) invalidTable[0].offset = 65536;
    fixture(path, {{0, invalidTable}});
    SdCardFont font;
    assert(!font.load(path.c_str()));
    clean(font);
  }
  std::filesystem::remove_all(dir);
  std::puts("SD font interval regression checks passed");
}
