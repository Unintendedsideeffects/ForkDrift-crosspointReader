#include "Arduino.h"
#include "HalGPIO.h"
#include "SPI.h"
#include "SdCardFont.h"

HardwareSerial Serial;
SPIClass SPI;
HalGPIO gpio;

// The screen harness registers no SD-card fonts, so these SdCardFont paths in
// GfxRenderer are never exercised. Stub them (SdCardFont.cpp is not linked).
bool SdCardFont::isOverflowGlyph(const EpdGlyph*) const { return false; }
const uint8_t* SdCardFont::getOverflowBitmap(const EpdGlyph*) const { return nullptr; }
SdCardFont* SdCardFont::fromMissCtx(void*) { return nullptr; }
