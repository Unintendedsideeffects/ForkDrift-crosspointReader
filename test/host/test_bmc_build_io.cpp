#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "HalStorage.h"
#include "Serialization.h"
#include "doctest/doctest.h"
#include "zip_deflate_fixtures.h"

namespace {

constexpr int kSpineCount = 256;
constexpr size_t kBuildCap = 4096;

std::string hrefFor(int i) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "ch-%03d.xhtml", i);
  return buf;
}

void writeFixture(const char* path, const uint8_t* data, size_t len) {
  HalFile file;
  REQUIRE(Storage.openFileForWrite("TEST", path, file));
  REQUIRE(file.write(data, len) == len);
}

template <typename Dest>
void writeSpineThrough(Dest& dest, const std::string& href) {
  serialization::writeString(dest, href);
  serialization::writePod(dest, static_cast<uint32_t>(0));
  serialization::writePod(dest, static_cast<int16_t>(-1));
}

}  // namespace

TEST_CASE("BufferedWriter 4KB path matches unbuffered bytes and issues far fewer writes") {
  Storage.reset();
  HalFile rawFile;
  HalFile bufFile;
  REQUIRE(Storage.openFileForWrite("TEST", "/raw.bin", rawFile));
  REQUIRE(Storage.openFileForWrite("TEST", "/buf.bin", bufFile));

  HalFile::resetIoCounters();
  for (int i = 0; i < kSpineCount; i++) {
    writeSpineThrough(rawFile, hrefFor(i));
  }
  const int unbufferedWrites = HalFile::writeCalls();

  serialization::BufferedWriter writer(bufFile, kBuildCap);
  HalFile::resetIoCounters();
  for (int i = 0; i < kSpineCount; i++) {
    writeSpineThrough(writer, hrefFor(i));
  }
  REQUIRE(writer.flush());
  const int bufferedWrites = HalFile::writeCalls();

  HalFile rawRead;
  HalFile bufRead;
  REQUIRE(Storage.openFileForRead("TEST", "/raw.bin", rawRead));
  REQUIRE(Storage.openFileForRead("TEST", "/buf.bin", bufRead));
  REQUIRE(rawRead.size() == bufRead.size());
  REQUIRE(rawRead.size() > kBuildCap);
  std::vector<uint8_t> rawBytes(rawRead.size());
  std::vector<uint8_t> bufBytes(bufRead.size());
  REQUIRE(rawRead.read(rawBytes.data(), rawBytes.size()) == rawBytes.size());
  REQUIRE(bufRead.read(bufBytes.data(), bufBytes.size()) == bufBytes.size());
  CHECK(rawBytes == bufBytes);
  CHECK(bufferedWrites < unbufferedWrites / 8);
  CHECK(bufferedWrites <= static_cast<int>((rawBytes.size() + kBuildCap - 1) / kBuildCap));
}

TEST_CASE("BufferedReader position tracks logical offset across seek") {
  Storage.reset();
  HalFile out;
  REQUIRE(Storage.openFileForWrite("TEST", "/seq.bin", out));
  std::vector<uint8_t> payload(1000);
  for (size_t i = 0; i < payload.size(); i++) {
    payload[i] = static_cast<uint8_t>(i & 0xFF);
  }
  REQUIRE(out.write(payload.data(), payload.size()) == payload.size());

  HalFile in;
  REQUIRE(Storage.openFileForRead("TEST", "/seq.bin", in));
  serialization::BufferedReader reader(in, 512);
  uint8_t scratch[10];
  REQUIRE(reader.read(scratch, 10) == 10);
  CHECK(reader.position() == 10);
  REQUIRE(reader.seek(600));
  CHECK(reader.position() == 600);
  REQUIRE(reader.read(scratch, 4) == 4);
  CHECK(scratch[0] == static_cast<uint8_t>(600 & 0xFF));
  CHECK(reader.position() == 604);
}

TEST_CASE("BufferedWriter flush failure is not success") {
  Storage.reset();
  HalFile file;
  REQUIRE(Storage.openFileForWrite("TEST", "/fail.bin", file));
  serialization::BufferedWriter writer(file, kBuildCap);
  const uint8_t data[] = {1, 2, 3, 4};
  REQUIRE(writer.write(data, sizeof(data)) == sizeof(data));
  HalFile::failNextWrites(1);
  CHECK_FALSE(writer.flush());
  CHECK(writer.failed());
  HalFile::resetIoCounters();
}

TEST_CASE("endContentOpfPass reports flush failure") {
  Storage.reset();
  Storage.mkdir("/bmc_flush");
  BookMetadataCache cache("/bmc_flush");
  REQUIRE(cache.beginWrite());
  REQUIRE(cache.beginContentOpfPass());
  cache.createSpineEntry("ch-000.xhtml");
  HalFile::failNextWrites(1);
  CHECK_FALSE(cache.endContentOpfPass());
  HalFile::resetIoCounters();
}

TEST_CASE("buildBookBin round-trips a large spine tmp and LUT matches wrapper positions") {
  Storage.reset();
  Storage.mkdir("/bmc_build");
  writeFixture("/bmc_build.epub", kZipStoredOnly, sizeof(kZipStoredOnly));

  BookMetadataCache cache("/bmc_build");
  REQUIRE(cache.beginWrite());
  REQUIRE(cache.beginContentOpfPass());
  HalFile::resetIoCounters();
  for (int i = 0; i < kSpineCount; i++) {
    cache.createSpineEntry(hrefFor(i));
  }
  REQUIRE(cache.endContentOpfPass());
  const int opfWrites = HalFile::writeCalls();
  CHECK(opfWrites >= 1);
  CHECK(opfWrites < kSpineCount);

  REQUIRE(cache.beginTocPass());
  cache.createTocEntry("One", hrefFor(0), "", 0);
  cache.createTocEntry("Fifty", hrefFor(50), "n", 1);
  REQUIRE(cache.endTocPass());
  REQUIRE(cache.endWrite());

  BookMetadataCache::BookMetadata meta;
  meta.title = "Omnibus";
  meta.language = "en";
  REQUIRE(cache.buildBookBin("/bmc_build.epub", meta));

  BookMetadataCache loaded("/bmc_build");
  REQUIRE(loaded.load());
  CHECK(loaded.getSpineCount() == kSpineCount);
  CHECK(loaded.getTocCount() == 2);
  CHECK(loaded.getSpineEntry(0).href == hrefFor(0));
  CHECK(loaded.getSpineEntry(50).href == hrefFor(50));
  CHECK(loaded.getSpineEntry(kSpineCount - 1).href == hrefFor(kSpineCount - 1));
  CHECK(loaded.getTocEntry(0).title == "One");
  CHECK(loaded.getTocEntry(1).title == "Fifty");
  CHECK(loaded.getTocEntry(1).anchor == "n");

  HalFile tmp;
  REQUIRE(Storage.openFileForRead("TEST", "/bmc_build/spine.bin.tmp", tmp));
  serialization::BufferedReader spineIn(tmp, kBuildCap);
  std::vector<uint32_t> tmpPositions;
  tmpPositions.reserve(static_cast<size_t>(kSpineCount));
  for (int i = 0; i < kSpineCount; i++) {
    tmpPositions.push_back(static_cast<uint32_t>(spineIn.position()));
    std::string href;
    uint32_t cum = 0;
    int16_t toc = -1;
    REQUIRE(serialization::readString(spineIn, href));
    REQUIRE(serialization::readPod(spineIn, cum));
    REQUIRE(serialization::readPod(spineIn, toc));
    CHECK(href == hrefFor(i));
  }
  const uint32_t spineBytes = static_cast<uint32_t>(spineIn.position());

  HalFile book;
  REQUIRE(Storage.openFileForRead("TEST", "/bmc_build/book.bin", book));
  uint8_t version = 0;
  uint32_t lutOffset = 0;
  uint16_t spineCount = 0;
  uint16_t tocCount = 0;
  REQUIRE(serialization::readPod(book, version));
  REQUIRE(serialization::readPod(book, lutOffset));
  REQUIRE(serialization::readPod(book, spineCount));
  REQUIRE(serialization::readPod(book, tocCount));
  CHECK(spineCount == kSpineCount);
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  REQUIRE(book.seek(lutOffset));
  for (int i = 0; i < kSpineCount; i++) {
    uint32_t lutPos = 0;
    REQUIRE(serialization::readPod(book, lutPos));
    CHECK(lutPos == tmpPositions[static_cast<size_t>(i)] + lutOffset + lutSize);
  }
  CHECK(spineBytes == tmpPositions.back() + 4 + hrefFor(kSpineCount - 1).size() + 4 + 2);
}
