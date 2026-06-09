#include "components/themes/pokemon/PokemonPartyTheme.h"

#if ENABLE_POKEMON_PARTY

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "components/icons/book24.h"
#include "fontIds.h"
#include "util/BookProgressDataStore.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#include "util/RecentBooksStore.h"

namespace {
constexpr int kCols = 2;
constexpr int kRows = 3;
constexpr int kSlots = kCols * kRows;
constexpr int kMargin = 10;
constexpr int kGap = 8;
constexpr int kPad = 6;
constexpr int kCorner = 10;
constexpr int kHpBarHeight = 10;
constexpr int kHpLabelW = 18;
constexpr int kHpLabelH = 12;
constexpr int kSelectBorder = 4;
constexpr int kStripeStep = 8;
constexpr int kCoverIconSize = 24;
constexpr int kSpriteSize = 34;

std::string upperName(const std::string& raw) {
  std::string out = raw;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](const char ch) { return static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); });
  return out;
}

void drawPartyBackground(const GfxRenderer& renderer, const Rect& rect) {
  for (int x = rect.x; x < rect.x + rect.width; x += kStripeStep) {
    renderer.fillRectDither(x, rect.y, 1, rect.height, Color::LightGray);
  }
}

void drawPokeball(const GfxRenderer& renderer, const int cx, const int cy, const int radius) {
  if (radius < 6) {
    return;
  }
  renderer.drawRoundedRect(cx - radius, cy - radius, radius * 2, radius * 2, 2, radius, true);
  renderer.drawLine(cx - radius, cy, cx - radius / 3, cy, 2, true);
  renderer.drawLine(cx + radius / 3, cy, cx + radius, cy, 2, true);
  const int inner = std::max(3, radius / 3);
  renderer.drawRoundedRect(cx - inner, cy - inner, inner * 2, inner * 2, 2, inner, true);
}

bool drawBmpInBox(const GfxRenderer& renderer, const std::string& path, const int x, const int y, const int size) {
  if (path.empty() || !Storage.exists(path.c_str())) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("PKM", path, file)) {
    return false;
  }
  Bitmap bitmap(file);
  bool drew = false;
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    renderer.drawBitmap1Bit(bitmap, x, y, size, size);
    drew = true;
  }
  file.close();
  return drew;
}

void drawBookIcon(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y) {
  if (drawBmpInBox(renderer, book.coverBmpPath, x, y, kCoverIconSize)) {
    return;
  }
  renderer.drawIcon(Book24Icon, x, y, kCoverIconSize, kCoverIconSize);
}

void drawHpBar(const GfxRenderer& renderer, const int x, const int y, const int width, const float percent) {
  renderer.fillRect(x, y, kHpLabelW, kHpLabelH, true);
  renderer.drawRect(x, y, kHpLabelW, kHpLabelH, true);
  renderer.drawText(SMALL_FONT_ID, x + 3, y + 1, "HP", false);

  const int barX = x + kHpLabelW + 4;
  const int barW = std::max(0, width - kHpLabelW - 4);
  if (barW <= 4) {
    return;
  }
  renderer.drawRect(barX, y + 1, barW, kHpBarHeight, true);
  const float frac = std::clamp(percent / 100.0f, 0.0f, 1.0f);
  const int fillW = static_cast<int>((barW - 2) * frac);
  if (fillW > 0) {
    renderer.fillRect(barX + 1, y + 2, fillW, kHpBarHeight - 2, true);
  }
}

void drawPartySlot(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                   const RecentBook& book, const bool selected) {
  renderer.fillRoundedRect(x, y, w, h, kCorner, Color::LightGray);
  renderer.drawRoundedRect(x, y, w, h, 2, kCorner, true);

  BookProgressDataStore::ProgressData pd;
  const float percent = BookProgressDataStore::loadProgress(book.path, pd) ? pd.percent : 0.0f;
  const int level = PokemonProgress::levelForPercent(percent);
  const PokemonAssignment assignment = PokemonProgress::loadForBook(book.path);

  const int iconX = x + kPad;
  const int iconY = y + kPad + 4;
  drawBookIcon(renderer, book, iconX, iconY);

  const int spriteX = iconX + kCoverIconSize + 4;
  const int spriteY = y + kPad + 2;
  bool drewSprite = false;
  if (assignment.valid) {
    const int speciesId = PokemonProgress::activeSpeciesId(assignment, level);
    drewSprite = drawBmpInBox(renderer, PokemonSpriteCache::spritePath(speciesId), spriteX, spriteY, kSpriteSize);
  }
  if (!drewSprite) {
    drewSprite = drawBmpInBox(renderer, book.coverBmpPath, spriteX, spriteY, kSpriteSize);
  }
  if (!drewSprite) {
    drawPokeball(renderer, spriteX + kSpriteSize / 2, spriteY + kSpriteSize / 2, kSpriteSize / 2 - 2);
  }

  const int textX = spriteX + kSpriteSize + kPad;
  const int textRight = x + w - kPad;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);

  const int nickMaxW = std::max(0, textRight - textX);
  const std::string nickname = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), nickMaxW);
  renderer.drawText(UI_12_FONT_ID, textX, y + kPad, nickname.c_str(), true);

  const int hpY = y + kPad + lineH + 4;
  drawHpBar(renderer, textX, hpY, textRight - textX, percent);

  const int bottomY = y + h - kPad - smallH;
  char levelText[16];
  std::snprintf(levelText, sizeof(levelText), "Lvl %d", level);
  renderer.drawText(SMALL_FONT_ID, x + kPad, bottomY, levelText, true);

  if (assignment.valid && !assignment.name.empty()) {
    const std::string species = upperName(assignment.name);
    const std::string speciesText = renderer.truncatedText(SMALL_FONT_ID, species.c_str(), textRight - textX);
    const int speciesW = renderer.getTextWidth(SMALL_FONT_ID, speciesText.c_str());
    renderer.drawText(SMALL_FONT_ID, textRight - speciesW, bottomY, speciesText.c_str(), true, EpdFontFamily::BOLD);
  }

  if (selected) {
    renderer.drawRoundedRect(x - 2, y - 2, w + 4, h + 4, kSelectBorder, kCorner + 2, true);
    renderer.drawRoundedRect(x - 1, y - 1, w + 2, h + 2, 1, kCorner + 1, true);
  }
}

void drawEmptySlot(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.drawRoundedRect(x, y, w, h, 1, kCorner, true);
}
}  // namespace

void PokemonPartyTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                            bool& /*coverRendered*/, bool& /*coverBufferStored*/,
                                            bool& /*bufferRestored*/, const std::function<bool()>& /*storeCoverBuffer*/,
                                            float /*progressPercent*/) const {
  drawPartyBackground(renderer, rect);

  const int bookCount = static_cast<int>(recentBooks.size());
  const int usedRows = bookCount > kCols ? kRows : 1;
  const int areaW = rect.width - 2 * kMargin;
  const int areaH = rect.height - 2 * kMargin;
  const int tileW = (areaW - kGap * (kCols - 1)) / kCols;
  const int tileH = (areaH - kGap * (usedRows - 1)) / usedRows;

  for (int i = 0; i < kSlots; ++i) {
    const int col = i % kCols;
    const int row = i / kCols;
    if (row >= usedRows) {
      continue;
    }

    const int x = rect.x + kMargin + col * (tileW + kGap);
    const int y = rect.y + kMargin + row * (tileH + kGap);
    const bool selected = selectorIndex == i;

    if (i >= bookCount) {
      drawEmptySlot(renderer, x, y, tileW, tileH);
      continue;
    }

    drawPartySlot(renderer, x, y, tileW, tileH, recentBooks[static_cast<size_t>(i)], selected);
  }
}

void PokemonPartyTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                       const std::function<std::string(int index)>& buttonLabel,
                                       const std::function<UIIcon(int index)>& rowIcon) const {
  ForkDriftTheme::drawButtonMenu(renderer, rect, buttonCount, selectedIndex, buttonLabel, rowIcon);
}

void PokemonPartyTheme::drawButtonHints(GfxRenderer& renderer, const char* /*btn1*/, const char* /*btn2*/,
                                        const char* /*btn3*/, const char* /*btn4*/,
                                        const bool /*allowInvertedText*/) const {
  (void)renderer;
}

#endif  // ENABLE_POKEMON_PARTY
