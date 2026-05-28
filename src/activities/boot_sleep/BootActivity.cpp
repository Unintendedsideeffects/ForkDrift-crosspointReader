#include "BootActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "BrandScreen.h"
#include "CrossPointSettings.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  bool renderedTransparent = false;
  if (Storage.exists("/sleep/transparent.bmp")) {
    HalFile file;
    if (Storage.openFileForRead("SLP", "/sleep/transparent.bmp", file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.clearScreen();
        renderer.drawBitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), 0, 0);
        renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
        renderedTransparent = true;
      }
      file.close();
    }
    // Delete the temporary transparent sleep screen screenshot on wake
    Storage.remove("/sleep/transparent.bmp");
  }

  if (!renderedTransparent) {
    renderer.clearScreen();
    BrandScreen::drawLogo(renderer, pageWidth, pageHeight);
    BrandScreen::drawTitle(renderer, pageHeight);
    BrandScreen::drawSubtitle(renderer, pageHeight, tr(STR_BOOTING));
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
    renderer.displayBuffer();
  }
}
