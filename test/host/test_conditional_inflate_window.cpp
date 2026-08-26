#include <cstdio>
#include <cstring>
#include <string>

#include "ZipFile.h"
#include "doctest/doctest.h"
#include "lib/Epub/Epub/BookMetadataCache.h"
#include "lib/InflateReader/InflateReader.h"
#include "lib/Serialization/Serialization.h"
#include "zip_deflate_fixtures.h"

namespace {

// Helper: write a minimal book.bin to mock storage via HalFile.
// Format: header(10) + metadata(5 strings) + hasDeflatedEntries(1) + LUT(spine+toc u32s)
void writeMinimalBookBin(const std::string& path, bool hasDeflated, int spineCount, int tocCount) {
  HalFile file;
  REQUIRE(Storage.openFileForWrite("TEST", path, file));

  serialization::writePod(file, static_cast<uint8_t>(BookMetadataCache::kFormatVersion));

  // lutOffset is computed below after metadata
  // Header sizes: version(1) + lutOffset(4) + spineCount(2) + tocCount(2) = 9
  // Metadata: 5 strings × (4 + content) + hasDeflatedEntries(1)
  const uint32_t headerSize = 9;
  const uint32_t metadataSize = sizeof(uint32_t) + 5 +  // title: "t" (4 len + 1 byte)
                                sizeof(uint32_t) + 1 +  // author: "" (4 len + 0 bytes)
                                sizeof(uint32_t) + 2 +  // language: "en" (4 len + 2 bytes)
                                sizeof(uint32_t) + 1 +  // coverItemHref: "" (4 len + 0 bytes)
                                sizeof(uint32_t) + 1 +  // textReferenceHref: "" (4 len + 0 bytes)
                                sizeof(uint8_t);        // hasDeflatedEntries
  const uint32_t lutOffset = headerSize + metadataSize;

  serialization::writePod(file, lutOffset);
  serialization::writePod(file, static_cast<uint16_t>(spineCount));
  serialization::writePod(file, static_cast<uint16_t>(tocCount));

  // Metadata strings
  const std::string title = "t";
  const std::string author;
  const std::string language = "en";
  const std::string coverHref;
  const std::string textRef;
  serialization::writeString(file, title);
  serialization::writeString(file, author);
  serialization::writeString(file, language);
  serialization::writeString(file, coverHref);
  serialization::writeString(file, textRef);

  // hasDeflatedEntries flag
  serialization::writePod(file, static_cast<uint8_t>(hasDeflated ? 1 : 0));

  // LUT: spine offsets + toc offsets (all zero; load() validates them
  // against file size but they are not exercised by getHasDeflatedEntries)
  for (int i = 0; i < spineCount + tocCount; i++) {
    serialization::writePod(file, static_cast<uint32_t>(0));
  }
}

}  // namespace

TEST_CASE("BookMetadataCache: hasDeflatedEntries round-trips as true") {
  Storage.reset();
  const std::string path = "/test_book_true/book.bin";
  Storage.mkdir("/test_book_true");

  writeMinimalBookBin(path, true, 0, 0);

  BookMetadataCache cache("/test_book_true");
  REQUIRE(cache.load());
  CHECK(cache.getHasDeflatedEntries() == true);
}

TEST_CASE("BookMetadataCache: hasDeflatedEntries round-trips as false") {
  Storage.reset();
  const std::string path = "/test_book_false/book.bin";
  Storage.mkdir("/test_book_false");

  writeMinimalBookBin(path, false, 0, 0);

  BookMetadataCache cache("/test_book_false");
  REQUIRE(cache.load());
  CHECK(cache.getHasDeflatedEntries() == false);
}

TEST_CASE("BookMetadataCache: version mismatch rejects old cache") {
  Storage.reset();
  const std::string path = "/test_old_cache/book.bin";
  Storage.mkdir("/test_old_cache");

  // Write a v9 book.bin (too few bytes for the hasDeflatedEntries field)
  HalFile file;
  REQUIRE(Storage.openFileForWrite("TEST", path, file));
  serialization::writePod(file, static_cast<uint8_t>(9));  // old version
  serialization::writePod(file, static_cast<uint32_t>(100));
  serialization::writePod(file, static_cast<uint16_t>(0));
  serialization::writePod(file, static_cast<uint16_t>(0));
  // Minimal strings that won't be fully read (version check fails first)
  const std::string empty;
  serialization::writeString(file, empty);
  serialization::writeString(file, empty);
  serialization::writeString(file, empty);
  serialization::writeString(file, empty);
  serialization::writeString(file, empty);

  BookMetadataCache cache("/test_old_cache");
  CHECK_FALSE(cache.load());
  CHECK(cache.getHasDeflatedEntries() == false);
}

TEST_CASE("InflateReader: ensureSharedWindow is idempotent and allocates") {
  // This test exercises the idempotency guarantee of ensureSharedWindow().
  // Note: shared window state is process-global; this test does NOT reset it
  // to avoid interfering with other test cases.
  const bool result = InflateReader::ensureSharedWindow();
  CHECK(result == true);
  // Calling again must not crash or fail
  CHECK(InflateReader::ensureSharedWindow() == true);
}

// --- ZipFile::hasAnyDeflated ------------------------------------------------
//
// Regression guard. The first implementation of this walk mis-stepped the
// central-directory field offsets: it skipped 36 bytes past the compression
// method and then read filename bytes as the name/extra/comment lengths, so it
// seeked by a garbage amount and hit EOF. Because the EPUB spec requires the
// "mimetype" entry to be first and STORED, the desync happened on entry one for
// essentially every book, and the function reported "nothing deflated" for books
// that are entirely deflated. Verified against a 9-book library: 4 of 6 sampled
// books returned the wrong answer.
//
// Both fixtures below open with a STORED "mimetype", reproducing that shape.

namespace {
void writeFixture(const char* path, const uint8_t* data, size_t len) {
  Storage.reset();
  HalFile f;
  Storage.openFileForWrite("TEST", path, f);
  f.write(data, len);
  f.close();
}
}  // namespace

TEST_CASE("hasAnyDeflated finds a deflated entry past a stored mimetype") {
  writeFixture("/book.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  // ZipFile stores its path by reference (ZipFile.h:42), so the string must
  // outlive it -- a literal would bind to a temporary and dangle.
  const std::string path = "/book.epub";
  ZipFile zip(path);
  CHECK(zip.hasAnyDeflated() == true);
}

TEST_CASE("hasAnyDeflated reports false for a stored-only zip") {
  writeFixture("/book.epub", kZipStoredOnly, sizeof(kZipStoredOnly));
  const std::string path = "/book.epub";
  ZipFile zip(path);
  CHECK(zip.hasAnyDeflated() == false);
}

TEST_CASE("hasAnyDeflated reports false for a file that is not a zip") {
  const uint8_t junk[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  writeFixture("/book.epub", junk, sizeof(junk));
  const std::string path = "/book.epub";
  ZipFile zip(path);
  CHECK(zip.hasAnyDeflated() == false);
}
