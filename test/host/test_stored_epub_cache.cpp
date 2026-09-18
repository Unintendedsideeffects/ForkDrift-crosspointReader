#include <cstring>
#include <string>
#include <vector>

#include "BuildScratch.h"
#include "Epub/StoredEpubCache.h"
#include "HalStorage.h"
#include "InflateStream.h"
#include "ZipFile.h"
#include "doctest/doctest.h"
#include "zip_deflate_fixtures.h"

namespace {

void writeFixture(const char* path, const uint8_t* data, const size_t len) {
  HalFile file;
  REQUIRE(Storage.openFileForWrite("TEST", path, file));
  REQUIRE(file.write(data, len) == len);
  file.close();
}

struct ScratchLoan {
  std::vector<uint8_t> bytes;
  explicit ScratchLoan(const size_t size) : bytes(size, 0) { buildscratch::lend(bytes.data(), bytes.size()); }
  ~ScratchLoan() { buildscratch::reclaim(); }
};

}  // namespace

TEST_CASE("inspect: deflated EPUB past a stored mimetype") {
  Storage.reset();
  writeFixture("/book.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  const std::string path = "/book.epub";
  ZipFile zip(path);
  const ZipInspect inspected = zip.inspect();
  CHECK(inspected.kind == ZipKind::HasDeflate);
  CHECK(inspected.hasDeflate);
  CHECK(inspected.entryCount == 2);
  CHECK(zipNeedsInflateWindow(inspected));
}

TEST_CASE("inspect: stored-only EPUB") {
  Storage.reset();
  writeFixture("/book.epub", kZipStoredOnly, sizeof(kZipStoredOnly));
  const std::string path = "/book.epub";
  ZipFile zip(path);
  const ZipInspect inspected = zip.inspect();
  CHECK(inspected.kind == ZipKind::StoredOnly);
  CHECK_FALSE(inspected.hasDeflate);
  CHECK(inspected.entryCount == 2);
  CHECK_FALSE(zipNeedsInflateWindow(inspected));
}

TEST_CASE("inspect: junk is corrupt") {
  Storage.reset();
  const uint8_t junk[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  writeFixture("/book.epub", junk, sizeof(junk));
  const std::string path = "/book.epub";
  CHECK(ZipFile(path).inspect().kind == ZipKind::Corrupt);
}

TEST_CASE("RequireBuildScratch refuses heap fallback") {
  InflateStream stream;
  CHECK_FALSE(stream.init(true, InflateStream::ScratchPolicy::RequireBuildScratch));
  CHECK_FALSE(stream.usedBuildScratch());
}

TEST_CASE("RequireBuildScratch succeeds when scratch is lent") {
  ScratchLoan loan(48000);
  InflateStream stream;
  REQUIRE(stream.init(true, InflateStream::ScratchPolicy::RequireBuildScratch));
  CHECK(stream.usedBuildScratch());
}

TEST_CASE("writeStoredArchive converts deflate fixture without a heap window") {
  Storage.reset();
  writeFixture("/book.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  ScratchLoan loan(48000);
  const std::string src = "/book.epub";
  ZipFile zip(src);
  REQUIRE(zip.writeStoredArchive("/stored.epub", "/stored.off"));
  CHECK_FALSE(Storage.exists("/stored.off"));

  const std::string dest = "/stored.epub";
  ZipFile stored(dest);
  const ZipInspect inspected = stored.inspect();
  CHECK(inspected.kind == ZipKind::StoredOnly);
  CHECK_FALSE(inspected.hasDeflate);
  CHECK(inspected.entryCount == 2);
  CHECK(stored.validateStoredArchive(2, inspected.storedOutputSize));

  size_t mimeSize = 0;
  uint8_t* mime = stored.readFileToMemory("mimetype", &mimeSize, false);
  REQUIRE(mime != nullptr);
  CHECK(mimeSize == 20);
  CHECK(std::memcmp(mime, "application/epub+zip", 20) == 0);
  free(mime);

  size_t xmlSize = 0;
  uint8_t* xml = stored.readFileToMemory("META-INF/container.xml", &xmlSize, false);
  REQUIRE(xml != nullptr);
  CHECK(xmlSize == 480);
  free(xml);
}

TEST_CASE("StoredEpubCache prepare converts once then select hits") {
  Storage.reset();
  writeFixture("/Books/helm.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  ScratchLoan loan(48000);
  const std::string first = stored_epub::prepare("/Books/helm.epub");
  REQUIRE(first.find("/.crosspoint/stored-epub/") == 0);
  REQUIRE(first.find(".epub") != std::string::npos);

  const std::string selected = stored_epub::select("/Books/helm.epub");
  CHECK(selected == first);

  ZipFile shadow(first);
  CHECK(shadow.inspect().kind == ZipKind::StoredOnly);
  CHECK_FALSE(zipNeedsInflateWindow(shadow.inspect()));
}

TEST_CASE("StoredEpubCache prepare leaves stored-only originals in place") {
  Storage.reset();
  writeFixture("/plain.epub", kZipStoredOnly, sizeof(kZipStoredOnly));
  const std::string path = stored_epub::prepare("/plain.epub");
  CHECK(path == "/plain.epub");
  CHECK(stored_epub::select("/plain.epub").empty());
}

TEST_CASE("StoredEpubCache prepare falls back without scratch") {
  Storage.reset();
  writeFixture("/Books/helm.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  const std::string path = stored_epub::prepare("/Books/helm.epub");
  CHECK(path == "/Books/helm.epub");
}

TEST_CASE("StoredEpubCache prepare refuses when the card is full") {
  Storage.reset();
  Storage.setFreeBytes(0);
  writeFixture("/Books/helm.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  ScratchLoan loan(48000);
  const std::string path = stored_epub::prepare("/Books/helm.epub");
  CHECK(path == "/Books/helm.epub");
}

TEST_CASE("StoredEpubCache invalidate drops a matching shadow") {
  Storage.reset();
  writeFixture("/Books/helm.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  ScratchLoan loan(48000);
  const std::string shadow = stored_epub::prepare("/Books/helm.epub");
  REQUIRE_FALSE(shadow.empty());
  REQUIRE(shadow != "/Books/helm.epub");
  stored_epub::invalidate("/Books/helm.epub");
  CHECK(stored_epub::select("/Books/helm.epub").empty());
}

TEST_CASE("StoredEpubCache source change invalidates the shadow") {
  Storage.reset();
  writeFixture("/Books/helm.epub", kZipWithDeflate, sizeof(kZipWithDeflate));
  ScratchLoan loan(48000);
  REQUIRE(stored_epub::prepare("/Books/helm.epub") != "/Books/helm.epub");
  writeFixture("/Books/helm.epub", kZipStoredOnly, sizeof(kZipStoredOnly));
  CHECK(stored_epub::select("/Books/helm.epub").empty());
}
