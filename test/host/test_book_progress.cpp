#include <cmath>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "doctest/doctest.h"
#include "include/BookCachePath.h"
#include "lib/Serialization/Serialization.h"
#include "src/util/BookProgressDataStore.h"
#include "test/mock/HalStorage.h"

namespace {
std::string buildCachePath(const char* prefix, const std::string& bookPath) {
  return BookCachePath::build("/.crosspoint", prefix, bookPath);
}

void writeTxtIndexFile(const std::string& path, uint32_t totalPages, uint8_t markdownFlag = 0) {
  FsFile f;
  CHECK(Storage.openFileForWrite("TST", path, f));
  serialization::writePod(f, static_cast<uint32_t>(0x54585449));
  serialization::writePod(f, static_cast<uint8_t>(3));
  serialization::writePod(f, static_cast<uint32_t>(0));
  serialization::writePod(f, static_cast<int32_t>(480));
  serialization::writePod(f, static_cast<int32_t>(20));
  serialization::writePod(f, static_cast<int32_t>(0));
  serialization::writePod(f, static_cast<int32_t>(0));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, markdownFlag);
  serialization::writePod(f, totalPages);
  f.close();
}

void writeMarkdownSectionFile(const std::string& path, uint16_t pageCount) {
  FsFile f;
  CHECK(Storage.openFileForWrite("TST", path, f));
  serialization::writePod(f, static_cast<uint8_t>(1));
  serialization::writePod(f, static_cast<int>(0));
  serialization::writePod(f, 1.0f);
  serialization::writePod(f, false);
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, static_cast<uint16_t>(480));
  serialization::writePod(f, static_cast<uint16_t>(800));
  serialization::writePod(f, false);
  serialization::writePod(f, static_cast<uint32_t>(1024));
  serialization::writePod(f, pageCount);
  serialization::writePod(f, static_cast<uint32_t>(0));
  f.close();
}

void writeEpubBookCache(const std::string& path, const std::vector<uint32_t>& cumulativeSizes) {
  FsFile f;
  CHECK(Storage.openFileForWrite("TST", path, f));
  serialization::writePod(f, BookMetadataCache::kFormatVersion);
  serialization::writePod(f, static_cast<uint32_t>(0));
  serialization::writePod(f, static_cast<uint16_t>(cumulativeSizes.size()));
  serialization::writePod(f, static_cast<uint16_t>(0));
  for (int i = 0; i < 5; ++i) {
    serialization::writeString(f, std::string());
  }
  for (size_t i = 0; i < cumulativeSizes.size(); ++i) {
    serialization::writePod(f, static_cast<uint32_t>(0));
  }
  for (size_t i = 0; i < cumulativeSizes.size(); ++i) {
    serialization::writeString(f, std::string("chapter-") + std::to_string(i));
    serialization::writePod(f, cumulativeSizes[i]);
    serialization::writePod(f, static_cast<int16_t>(-1));
  }
  f.close();
}

void writeXtcHeaderFile(const std::string& path, uint16_t pageCount) {
  FsFile f;
  CHECK(Storage.openFileForWrite("TST", path, f));
  serialization::writePod(f, static_cast<uint32_t>(0x00435458));
  serialization::writePod(f, static_cast<uint8_t>(1));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, pageCount);
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, static_cast<uint32_t>(0));
  serialization::writePod(f, static_cast<uint64_t>(0));
  serialization::writePod(f, static_cast<uint64_t>(0));
  serialization::writePod(f, static_cast<uint64_t>(0));
  serialization::writePod(f, static_cast<uint64_t>(0));
  serialization::writePod(f, static_cast<uint32_t>(0));
  serialization::writePod(f, static_cast<uint32_t>(0));
  f.close();
}
}  // namespace

TEST_CASE("testBookProgressDataStore") {
  Storage.reset();

  {
    const std::string bookPath = "/books/demo.txt";
    const std::string cachePath = buildCachePath("txt_", bookPath);

    FsFile progressFile;
    CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
    const uint8_t progressBytes[4] = {9, 0, 0, 0};
    progressFile.write(progressBytes, sizeof(progressBytes));
    progressFile.close();
    writeTxtIndexFile(cachePath + "/index.bin", 40);

    BookProgressDataStore::ProgressData progress;
    CHECK(BookProgressDataStore::loadProgress(bookPath, progress));
    CHECK(progress.kind == BookProgressDataStore::BookKind::Txt);
    CHECK(progress.page == 10);
    CHECK(progress.pageCount == 40);
    CHECK(std::fabs(progress.percent - 25.0f) < 0.01f);
    CHECK(BookProgressDataStore::formatPositionLabel(progress) == "10/40 25%");
  }

  {
    const std::string bookPath = "/books/demo.md";
    const std::string cachePath = buildCachePath("md_", bookPath);

    FsFile progressFile;
    CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
    const uint8_t progressBytes[4] = {4, 0, 0, 0};
    progressFile.write(progressBytes, sizeof(progressBytes));
    progressFile.close();
    writeMarkdownSectionFile(cachePath + "/md_section.bin", 20);

    BookProgressDataStore::ProgressData progress;
    CHECK(BookProgressDataStore::loadProgress(bookPath, progress));
    CHECK(progress.kind == BookProgressDataStore::BookKind::Markdown);
    CHECK(progress.page == 5);
    CHECK(progress.pageCount == 20);
    CHECK(std::fabs(progress.percent - 25.0f) < 0.01f);
  }

  {
    const std::string bookPath = "/books/fallback.md";
    const std::string cachePath = buildCachePath("txt_", bookPath);

    FsFile progressFile;
    CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
    const uint8_t progressBytes[4] = {1, 0, 0, 0};
    progressFile.write(progressBytes, sizeof(progressBytes));
    progressFile.close();
    writeTxtIndexFile(cachePath + "/index.bin", 10, 1);

    BookProgressDataStore::ProgressData progress;
    CHECK(BookProgressDataStore::loadProgress(bookPath, progress));
    CHECK(progress.kind == BookProgressDataStore::BookKind::Markdown);
    CHECK(progress.page == 2);
    CHECK(progress.pageCount == 10);
    CHECK(std::fabs(progress.percent - 20.0f) < 0.01f);
  }

  {
    const std::string bookPath = "/books/demo.epub";
    const std::string cachePath = buildCachePath("epub_", bookPath);

    FsFile progressFile;
    CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
    const uint8_t progressBytes[6] = {1, 0, 4, 0, 10, 0};
    progressFile.write(progressBytes, sizeof(progressBytes));
    progressFile.close();
    writeEpubBookCache(cachePath + "/book.bin", {100, 300, 600});

    BookProgressDataStore::ProgressData progress;
    CHECK(BookProgressDataStore::loadProgress(bookPath, progress));
    CHECK(progress.kind == BookProgressDataStore::BookKind::Epub);
    CHECK(progress.spineIndex == 1);
    CHECK(progress.page == 5);
    CHECK(progress.pageCount == 10);
    CHECK(std::fabs(progress.percent - 33.33f) < 0.02f);
    CHECK(BookProgressDataStore::formatPositionLabel(progress) == "Ch 2 5/10 33%");
  }

  {
    const std::string bookPath = "/books/demo.xtc";
    const std::string cachePath = buildCachePath("xtc_", bookPath);

    FsFile progressFile;
    CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
    const uint8_t progressBytes[4] = {49, 0, 0, 0};
    progressFile.write(progressBytes, sizeof(progressBytes));
    progressFile.close();
    writeXtcHeaderFile(bookPath, 100);

    BookProgressDataStore::ProgressData progress;
    CHECK(BookProgressDataStore::loadProgress(bookPath, progress));
    CHECK(progress.kind == BookProgressDataStore::BookKind::Xtc);
    CHECK(progress.page == 50);
    CHECK(progress.pageCount == 100);
    CHECK(std::fabs(progress.percent - 50.0f) < 0.01f);
  }

  BookProgressDataStore::ProgressData missingProgress;
  CHECK(!BookProgressDataStore::loadProgress("/books/missing.epub", missingProgress));
  CHECK(BookProgressDataStore::supportsBookPath("/books/demo.epub"));
  CHECK(!BookProgressDataStore::supportsBookPath("/books/demo.pdf"));
}
