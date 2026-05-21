#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "BrandScreen.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  BrandScreen::drawLogo(renderer, pageWidth, pageHeight);
  BrandScreen::drawTitle(renderer, pageHeight);
  BrandScreen::drawSubtitle(renderer, pageHeight, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
