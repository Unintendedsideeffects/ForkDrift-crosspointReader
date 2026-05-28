#include <cstdint>
#include <vector>

#include "Bitmap.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

namespace {

// Build a 1-bit 480×800 BMP byte-for-byte matching the Pokemon wallpaper
// plugin's canvasToBmpBlob() output (HEADER_SIZE=62, rowStride=60, bottom-up).
std::vector<uint8_t> makePokedexBmp(int width = 480, int height = 800) {
  const int rowStride = ((width * 1 + 31) / 32) * 4;  // = 60 for width=480
  const int pixelDataSize = rowStride * height;
  const int fileSize = 62 + pixelDataSize;

  std::vector<uint8_t> bmp(fileSize, 0);
  uint8_t* p = bmp.data();

  auto writeLE16 = [](uint8_t* dst, uint16_t v) {
    dst[0] = v & 0xFF;
    dst[1] = (v >> 8) & 0xFF;
  };
  auto writeLE32 = [](uint8_t* dst, uint32_t v) {
    dst[0] = v & 0xFF;
    dst[1] = (v >> 8) & 0xFF;
    dst[2] = (v >> 16) & 0xFF;
    dst[3] = (v >> 24) & 0xFF;
  };

  // File header (14 bytes)
  p[0] = 'B';
  p[1] = 'M';
  writeLE32(p + 2, static_cast<uint32_t>(fileSize));
  writeLE16(p + 6, 0);    // reserved1
  writeLE16(p + 8, 0);    // reserved2
  writeLE32(p + 10, 62);  // pixel data offset (14 + 40 + 8)

  // DIB header (40 bytes, starting at offset 14)
  writeLE32(p + 14, 40);  // biSize
  writeLE32(p + 18, static_cast<uint32_t>(width));
  writeLE32(p + 22, static_cast<uint32_t>(height));  // positive = bottom-up
  writeLE16(p + 26, 1);                              // biPlanes
  writeLE16(p + 28, 1);                              // biBitCount = 1
  writeLE32(p + 30, 0);                              // biCompression = BI_RGB
  writeLE32(p + 34, 0);                              // biSizeImage (can be 0 for BI_RGB)
  writeLE32(p + 38, 0);                              // biXPelsPerMeter
  writeLE32(p + 42, 0);                              // biYPelsPerMeter
  writeLE32(p + 46, 2);                              // biClrUsed = 2
  writeLE32(p + 50, 0);                              // biClrImportant

  // Palette (8 bytes at offset 54): black then white
  p[54] = 0;
  p[55] = 0;
  p[56] = 0;
  p[57] = 0;  // color 0: black (B,G,R,X)
  p[58] = 255;
  p[59] = 255;
  p[60] = 255;
  p[61] = 0;  // color 1: white (B,G,R,X)

  // Pixel data: all zeros (all black) — bytes 62 onwards
  return bmp;
}

HalFile fileFromBmp(const std::vector<uint8_t>& bmp) {
  auto buf = std::make_shared<std::vector<uint8_t>>(bmp);
  return HalFile::forFile("pokedex_0001_bulbasaur_x4.bmp", buf);
}

}  // namespace

TEST_CASE("pokedex BMP 480x800 parses without error") {
  auto bmp = makePokedexBmp();
  auto file = fileFromBmp(bmp);
  Bitmap bitmap(file, false);
  CHECK(bitmap.parseHeaders() == BmpReaderError::Ok);
  CHECK(bitmap.getWidth() == 480);
  CHECK(bitmap.getHeight() == 800);
  CHECK(bitmap.is1Bit());
  CHECK(bitmap.getRowBytes() == 60);
  CHECK_FALSE(bitmap.isTopDown());
}

TEST_CASE("pokedex BMP first row reads back successfully") {
  auto bmp = makePokedexBmp();
  auto file = fileFromBmp(bmp);
  Bitmap bitmap(file, false);
  REQUIRE(bitmap.parseHeaders() == BmpReaderError::Ok);

  std::vector<uint8_t> outputRow(120, 0);
  std::vector<uint8_t> rowBuffer(60, 0);
  CHECK(bitmap.readNextRow(outputRow.data(), rowBuffer.data()) == BmpReaderError::Ok);
}

TEST_CASE("pokedex BMP all 800 rows read without error") {
  auto bmp = makePokedexBmp();
  auto file = fileFromBmp(bmp);
  Bitmap bitmap(file, false);
  REQUIRE(bitmap.parseHeaders() == BmpReaderError::Ok);

  std::vector<uint8_t> outputRow(120, 0);
  std::vector<uint8_t> rowBuffer(60, 0);
  for (int row = 0; row < 800; row++) {
    INFO("row " << row);
    CHECK(bitmap.readNextRow(outputRow.data(), rowBuffer.data()) == BmpReaderError::Ok);
  }
}

TEST_CASE("pokedex BMP stored in mock storage is found by open()") {
  Storage.reset();

  auto bmp = makePokedexBmp();
  const std::string path = "/sleep/pokedex/pokedex_0001_bulbasaur_x4.bmp";
  Storage.mkdir("/sleep/pokedex");

  // Write the BMP bytes directly into mock storage
  HalFile f;
  REQUIRE(Storage.openFileForWrite("TEST", path, f));
  f.write(bmp.data(), bmp.size());

  // Verify the directory is visible
  auto dir = Storage.open("/sleep/pokedex");
  REQUIRE(dir);
  REQUIRE(dir.isDirectory());

  // Verify openNextFile returns the BMP
  auto entry = dir.openNextFile();
  REQUIRE(entry);
  CHECK_FALSE(entry.isDirectory());
  char name[64];
  entry.getName(name, sizeof(name));
  CHECK(std::string(name).find("bulbasaur") != std::string::npos);
}

TEST_CASE("pokedex BMP in mock storage parses via HalStorage::openFileForRead") {
  Storage.reset();

  auto bmp = makePokedexBmp();
  const std::string path = "/sleep/pokedex/pokedex_0001_bulbasaur_x4.bmp";
  Storage.mkdir("/sleep/pokedex");

  {
    HalFile f;
    REQUIRE(Storage.openFileForWrite("TEST", path, f));
    f.write(bmp.data(), bmp.size());
  }

  HalFile f;
  REQUIRE(Storage.openFileForRead("TEST", path, f));
  Bitmap bitmap(f, false);
  CHECK(bitmap.parseHeaders() == BmpReaderError::Ok);
  CHECK(bitmap.getWidth() == 480);
  CHECK(bitmap.getHeight() == 800);
}
