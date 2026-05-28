#include <cstring>
#include <vector>

#include "Bitmap.h"
#include "BitmapHelpers.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

TEST_CASE("Pokedex 1-bit monochrome BMP validity test") {
  Storage.reset();

  // Create a 480x800 1-bit monochrome BMP in Mock Storage
  BmpHeader header;
  createBmpHeader(&header, 480, 800, BmpRowOrder::BottomUp);

  // Validate the standard header values generated are exactly correct.
  // Copy packed fields into locals before comparing — packed-struct member references
  // trigger UBSAN alignment warnings on x86-64 (and faults on RISC-V ESP32-C3).
  uint16_t bfType = header.fileHeader.bfType;
  int32_t biWidth, biHeight;
  uint16_t biBitCount, biPlanes;
  uint32_t biCompression;
  std::memcpy(&biWidth, &header.infoHeader.biWidth, 4);
  std::memcpy(&biHeight, &header.infoHeader.biHeight, 4);
  std::memcpy(&biBitCount, &header.infoHeader.biBitCount, 2);
  std::memcpy(&biPlanes, &header.infoHeader.biPlanes, 2);
  std::memcpy(&biCompression, &header.infoHeader.biCompression, 4);
  CHECK(bfType == 0x4D42);
  CHECK(biWidth == 480);
  CHECK(biHeight == 800);
  CHECK(biBitCount == 1);
  CHECK(biPlanes == 1);
  CHECK(biCompression == 0);

  // Populate color palette (0: black, 1: white)
  header.colors[0] = {0, 0, 0, 0};
  header.colors[1] = {255, 255, 255, 0};

  // 1-bit BMP row stride must be padded to 4-byte boundaries
  int rowStride = ((480 + 31) / 32) * 4;  // 60 bytes
  int imageSize = rowStride * 800;        // 48000 bytes
  int fileSize = sizeof(BmpHeader) + imageSize;

  // Let's create a buffer for the entire BMP file
  std::vector<uint8_t> bmpData(fileSize, 0);
  std::memcpy(bmpData.data(), &header, sizeof(BmpHeader));

  // Write some dummy white/black pixel patterns
  for (int y = 0; y < 800; ++y) {
    uint8_t* row = bmpData.data() + sizeof(BmpHeader) + y * rowStride;
    for (int x = 0; x < 60; ++x) {
      row[x] = (x + y) % 2 ? 0x55 : 0xAA;  // Checkerboard 1-bit patterns
    }
  }

  // Save the BMP to '/sleep/pokedex/pokedex_0150_mewtwo_x4.bmp'
  const char* filePath = "/sleep/pokedex/pokedex_0150_mewtwo_x4.bmp";
  std::string fileContent(reinterpret_cast<char*>(bmpData.data()), fileSize);
  CHECK(Storage.writeFile(filePath, fileContent));

  // Open the file and parse it with Bitmap class
  HalFile file;
  CHECK(Storage.openFileForRead("BMP", filePath, file));

  Bitmap bitmap(file);
  BmpReaderError err = bitmap.parseHeaders();
  CHECK(err == BmpReaderError::Ok);

  CHECK(bitmap.getWidth() == 480);
  CHECK(bitmap.getHeight() == 800);
  CHECK(bitmap.getBpp() == 1);
  CHECK(bitmap.is1Bit() == true);
  CHECK(bitmap.hasGreyscale() == false);

  file.close();
}
