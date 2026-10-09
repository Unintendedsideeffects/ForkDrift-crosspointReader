#pragma once

#include <cstdint>
#include <cstring>
#include <memory>

#include "../Memory/Memory.h"

struct BmpHeader;

// Helper functions
uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
uint8_t quantize1bit(int gray, int x, int y);
int adjustPixel(int gray);

enum class BmpRowOrder { BottomUp, TopDown };

// Populates a 1-bit BMP header in the provided memory.
void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

// 1-bit Atkinson dithering - better quality than noise dithering for thumbnails
// Error distribution pattern (same as 2-bit but quantizes to 2 levels):
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    // The three error rows share one zeroed block: one heap hole and one free
    // instead of three interleaved allocations (upstream 3555ff556).
    if (width <= 0) return;
    const size_t rowSize = static_cast<size_t>(width) + 4;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 3);
    if (!errorRows) return;
    errorRow0 = errorRows.get();      // Current row
    errorRow1 = errorRow0 + rowSize;  // Next row
    errorRow2 = errorRow1 + rowSize;  // Row after next
  }

  // EXPLICITLY DELETE THE COPY CONSTRUCTOR
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  // EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  bool isReady() const { return errorRow0 && errorRow1 && errorRow2; }

  uint8_t processPixel(int gray, int x) {
    // Apply brightness/contrast/gamma adjustments
    gray = adjustPixel(gray);

    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 2 levels (1-bit): 0 = black, 1 = white
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  std::unique_ptr<int16_t[]> errorRows;  // owns all three rows
  int16_t* errorRow0 = nullptr;
  int16_t* errorRow1 = nullptr;
  int16_t* errorRow2 = nullptr;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width) : width(width) {
    // The three error rows share one zeroed block: one heap hole and one free
    // instead of three interleaved allocations (upstream 3555ff556).
    if (width <= 0) return;
    const size_t rowSize = static_cast<size_t>(width) + 4;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 3);
    if (!errorRows) return;
    errorRow0 = errorRows.get();      // Current row
    errorRow1 = errorRow0 + rowSize;  // Next row
    errorRow2 = errorRow1 + rowSize;  // Row after next
  }
  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  bool isReady() const { return errorRow0 && errorRow1 && errorRow2; }

  uint8_t processPixel(int gray, int x) {
    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to the X4 e-ink tuned 4-level palette.
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 30) {
      quantized = 0;
      quantizedValue = 15;
    } else if (adjusted < 50) {
      quantized = 1;
      quantizedValue = 30;
    } else if (adjusted < 140) {
      quantized = 2;
      quantizedValue = 80;
    } else {
      quantized = 3;
      quantizedValue = 210;
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  std::unique_ptr<int16_t[]> errorRows;  // owns all three rows
  int16_t* errorRow0 = nullptr;
  int16_t* errorRow1 = nullptr;
  int16_t* errorRow2 = nullptr;
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width) : width(width), rowCount(0) {
    // Both error rows share one zeroed block (+2 each for boundary handling).
    if (width <= 0) return;
    const size_t rowSize = static_cast<size_t>(width) + 2;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 2);
    if (!errorRows) return;
    errorCurRow = errorRows.get();
    errorNextRow = errorCurRow + rowSize;
  }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  FloydSteinbergDitherer(const FloydSteinbergDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer& other) = delete;

  bool isReady() const { return errorCurRow && errorNextRow; }

  // Process a single pixel and return quantized 2-bit value
  // x is the logical x position (0 to width-1), direction handled internally
  uint8_t processPixel(int gray, int x) {
    // Add accumulated error to this pixel
    int adjusted = gray + errorCurRow[x + 1];

    // Clamp to valid range
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to the X4 e-ink tuned 4-level palette.
    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 30) {
      quantized = 0;
      quantizedValue = 15;
    } else if (adjusted < 50) {
      quantized = 1;
      quantizedValue = 30;
    } else if (adjusted < 140) {
      quantized = 2;
      quantizedValue = 80;
    } else {
      quantized = 3;
      quantizedValue = 210;
    }

    // Calculate error
    int error = adjusted - quantizedValue;

    // Distribute error to neighbors (serpentine: direction-aware)
    if (!isReverseRow()) {
      // Left to right: standard distribution
      // Right: 7/16
      errorCurRow[x + 2] += (error * 7) >> 4;
      // Bottom-left: 3/16
      errorNextRow[x] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-right: 1/16
      errorNextRow[x + 2] += (error) >> 4;
    } else {
      // Right to left: mirrored distribution
      // Left: 7/16
      errorCurRow[x] += (error * 7) >> 4;
      // Bottom-right: 3/16
      errorNextRow[x + 2] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-left: 1/16
      errorNextRow[x] += (error) >> 4;
    }

    return quantized;
  }

  // Call at the end of each row to swap buffers
  void nextRow() {
    // Swap buffers
    int16_t* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    // Clear the next row buffer
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount++;
  }

  // Check if current row should be processed in reverse
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  // Reset for a new image or MCU block
  void reset() {
    memset(errorCurRow, 0, (width + 2) * sizeof(int16_t));
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount = 0;
  }

 private:
  int width;
  int rowCount;
  std::unique_ptr<int16_t[]> errorRows;  // owns both rows
  int16_t* errorCurRow = nullptr;
  int16_t* errorNextRow = nullptr;
};
