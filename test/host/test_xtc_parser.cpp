#include <cstdint>
#include <vector>

#include "doctest/doctest.h"
#include "lib/Serialization/Serialization.h"
#include "lib/Xtc/Xtc/XtcParser.h"
#include "test/mock/HalStorage.h"

namespace {
void writePageHeader(FsFile& file, uint32_t magic, uint16_t width, uint16_t height, uint8_t colorMode,
                     uint8_t compression, uint32_t dataSize) {
  serialization::writePod(file, magic);
  serialization::writePod(file, width);
  serialization::writePod(file, height);
  serialization::writePod(file, colorMode);
  serialization::writePod(file, compression);
  serialization::writePod(file, dataSize);
  serialization::writePod(file, static_cast<uint64_t>(0));
}

void writeModernXtcFile(const char* path, uint8_t versionMajor = 1, uint8_t versionMinor = 0,
                        uint64_t pageTableOffset = sizeof(xtc::XtcHeader)) {
  FsFile file;
  CHECK(Storage.openFileForWrite("TST", path, file));

  serialization::writePod(file, xtc::XTC_MAGIC);
  serialization::writePod(file, versionMajor);
  serialization::writePod(file, versionMinor);
  serialization::writePod(file, static_cast<uint16_t>(1));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint32_t>(1));
  serialization::writePod(file, static_cast<uint64_t>(0));
  serialization::writePod(file, pageTableOffset);
  serialization::writePod(file, pageTableOffset + sizeof(xtc::PageTableEntry));
  serialization::writePod(file, static_cast<uint64_t>(0));
  serialization::writePod(file, static_cast<uint32_t>(0));
  serialization::writePod(file, static_cast<uint32_t>(0));

  if (pageTableOffset > sizeof(xtc::XtcHeader)) {
    std::vector<uint8_t> padding(static_cast<size_t>(pageTableOffset - sizeof(xtc::XtcHeader)), 0);
    file.write(padding.data(), padding.size());
  }

  serialization::writePod(file, pageTableOffset + sizeof(xtc::PageTableEntry));
  serialization::writePod(file, static_cast<uint32_t>(sizeof(xtc::XtgPageHeader) + 1));
  serialization::writePod(file, static_cast<uint16_t>(8));
  serialization::writePod(file, static_cast<uint16_t>(1));

  writePageHeader(file, xtc::XTG_MAGIC, 8, 1, 0, 0, 1);
  const uint8_t pixelByte = 0xAA;
  file.write(&pixelByte, 1);
  file.close();
}

// Pre-chapterOffset writers ended the header at 0x30 and put the page table
// there. hasChapters is set deliberately: there is no chapter field in this
// layout, so the parser must not believe it.
void writeLegacyXtcFile(const char* path) {
  FsFile file;
  CHECK(Storage.openFileForWrite("TST", path, file));

  constexpr uint64_t pageTableOffset = xtc::XTC_LEGACY_HEADER_SIZE;
  constexpr uint64_t dataOffset = pageTableOffset + sizeof(xtc::PageTableEntry);

  serialization::writePod(file, xtc::XTC_MAGIC);
  serialization::writePod(file, static_cast<uint8_t>(1));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint16_t>(1));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint8_t>(0));
  serialization::writePod(file, static_cast<uint8_t>(1));  // hasChapters
  serialization::writePod(file, static_cast<uint32_t>(1));
  serialization::writePod(file, static_cast<uint64_t>(0));
  serialization::writePod(file, pageTableOffset);
  serialization::writePod(file, dataOffset);
  serialization::writePod(file, static_cast<uint64_t>(0));
  // Header ends here at 0x30 — no chapterOffset, no padding.

  serialization::writePod(file, dataOffset);
  serialization::writePod(file, static_cast<uint32_t>(sizeof(xtc::XtgPageHeader) + 1));
  serialization::writePod(file, static_cast<uint16_t>(8));
  serialization::writePod(file, static_cast<uint16_t>(1));

  writePageHeader(file, xtc::XTG_MAGIC, 8, 1, 0, 0, 1);
  const uint8_t pixelByte = 0xAA;
  file.write(&pixelByte, 1);
  file.close();
}
}  // namespace

TEST_CASE("XtcParser accepts modern 1.0 headers") {
  Storage.reset();
  writeModernXtcFile("/books/modern.xtc");

  xtc::XtcParser parser;
  CHECK(parser.open("/books/modern.xtc") == xtc::XtcError::OK);
  CHECK(parser.getPageCount() == 1);
  CHECK(parser.getWidth() == 8);
  CHECK(parser.getHeight() == 1);

  uint8_t page[1] = {0};
  CHECK(parser.loadPage(0, page, sizeof(page)) == sizeof(page));
  CHECK(page[0] == 0xAA);
}

TEST_CASE("XtcParser rejects byte-swapped legacy version headers") {
  Storage.reset();
  writeModernXtcFile("/books/swapped-version.xtc", 0, 1);

  xtc::XtcParser parser;
  CHECK(parser.open("/books/swapped-version.xtc") == xtc::XtcError::INVALID_VERSION);
}

TEST_CASE("XtcParser opens legacy 48-byte header files") {
  Storage.reset();
  writeLegacyXtcFile("/books/legacy-table.xtc");

  xtc::XtcParser parser;
  CHECK(parser.open("/books/legacy-table.xtc") == xtc::XtcError::OK);
  CHECK(parser.getPageCount() == 1);
  CHECK(parser.getWidth() == 8);
  CHECK(parser.getHeight() == 1);
  // There is no chapter table in this layout, so the flag must be ignored
  // rather than sending the parser after bytes that belong to the page table.
  CHECK_FALSE(parser.hasChapters());

  uint8_t page[1] = {0};
  CHECK(parser.loadPage(0, page, sizeof(page)) == sizeof(page));
  CHECK(page[0] == 0xAA);
}

TEST_CASE("XtcParser rejects a page table that starts inside the header") {
  Storage.reset();
  writeModernXtcFile("/books/overlapping-table.xtc", 1, 0, 16);

  xtc::XtcParser parser;
  CHECK(parser.open("/books/overlapping-table.xtc") == xtc::XtcError::CORRUPTED_HEADER);
}
