#include <GfxRenderer.h>

#include "Epub/blocks/ImageBlock.h"

// The section-cache host tests exercise persistence only. These narrow stubs
// satisfy the render-side vtables without pulling the display and decoder stack
// into the ASan/UBSan host binary.
bool GfxRenderer::isFontCacheScanning() const { return false; }
void GfxRenderer::drawLine(int, int, int, int, bool) const {}
void GfxRenderer::drawLine(int, int, int, int, int, bool) const {}
void GfxRenderer::drawRect(int, int, int, int, bool) const {}
int GfxRenderer::getTextWidth(int, const char*, EpdFontFamily::Style, BidiUtils::BidiBaseDir) const { return 0; }
void GfxRenderer::drawText(int, int, int, const char*, bool, EpdFontFamily::Style, BidiUtils::BidiBaseDir) const {}
// getTextAdvanceX is deliberately NOT stubbed here: test/mock/GfxRendererTestStub.cpp
// provides a measuring implementation that the ParsedText layout tests assert exact
// pixel arithmetic against. Defining a return-0 placeholder as well is a duplicate
// symbol at link time, and the persistence tests in this file never measure text.
int GfxRenderer::getFontAscenderSize(int) const { return 0; }
void ImageBlock::render(GfxRenderer&, int, int) {}
