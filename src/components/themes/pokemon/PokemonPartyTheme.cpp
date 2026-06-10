#include "components/themes/pokemon/PokemonPartyTheme.h"

#if ENABLE_POKEMON_PARTY

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "components/UITheme.h"
#include "components/icons/book24.h"
#include "components/icons/calendar.h"
#include "components/icons/folder.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "fontIds.h"
#include "util/BookProgressDataStore.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#include "util/RecentBooksStore.h"

namespace {
constexpr int kCols = 2;
constexpr int kRows = 3;
constexpr int kSlots = kCols * kRows;

struct CachedSlotData {
  bool valid = false;
  float percent = 0.0f;
  int level = 1;
  int speciesId = 0;
  std::string speciesName;
  std::string spritePath;
};

std::unordered_map<std::string, CachedSlotData> g_partySlotCache;
constexpr int kMargin = 10;
constexpr int kGap = 8;
constexpr int kPad = 6;
constexpr int kCorner = 10;
constexpr int kHpBarHeight = 10;
constexpr int kHpLabelW = 18;
constexpr int kHpLabelH = 12;
constexpr int kSelectBorder = 4;
constexpr int kStripeStep = 8;
constexpr int kCoverIconSize = 56;
constexpr int kMenuCols = 2;
constexpr int kMenuIconSize = 24;
constexpr int kMenuCornerRadius = 6;
constexpr int kMenuHPadding = 8;
constexpr int kMenuLastInset = 8;

const uint8_t* menuIconFor(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Calendar:
      return CalendarIcon;
    case UIIcon::Text:
      return Text24Icon;
    case UIIcon::Book:
      return Book24Icon;
    default:
      return nullptr;
  }
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

bool drawSpriteInBox(const GfxRenderer& renderer, const std::string& path, const int x, const int y, const int size) {
  if (path.empty() || !Storage.exists(path.c_str())) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("PKM", path, file)) {
    return false;
  }
  Bitmap bitmap(file);
  bool drew = false;
  if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.is1Bit()) {
    renderer.drawBitmap1Bit(bitmap, x, y, size, size);
    drew = true;
  }
  file.close();
  return drew;
}

void drawBookIcon(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int size) {
  if (drawBmpInBox(renderer, book.coverBmpPath, x, y, size)) {
    return;
  }
  const int iconX = x + (size - 24) / 2;
  const int iconY = y + (size - 24) / 2;
  renderer.drawIcon(Book24Icon, iconX, iconY, 24, 24);
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

  CachedSlotData cachedData;
  auto it = g_partySlotCache.find(book.path);
  if (it != g_partySlotCache.end()) {
    cachedData = it->second;
  } else {
    BookProgressDataStore::ProgressData pd;
    cachedData.percent = BookProgressDataStore::loadProgress(book.path, pd) ? pd.percent : 0.0f;
    cachedData.level = PokemonProgress::levelForPercent(cachedData.percent);

    PokemonAssignment assignment = PokemonProgress::loadForBook(book.path);
    cachedData.valid = assignment.valid;
    if (assignment.valid) {
      cachedData.speciesId = PokemonProgress::activeSpeciesId(assignment, cachedData.level);
      cachedData.speciesName = assignment.name;
    } else {
      cachedData.speciesId = 0;
      cachedData.speciesName = "";
    }

    if (cachedData.speciesId > 0) {
      cachedData.spritePath = PokemonSpriteCache::spritePath(cachedData.speciesId);
    } else {
      cachedData.spritePath = "";
    }

    g_partySlotCache[book.path] = cachedData;
  }

  // 1. Title row first across full tile width
  const int titleMaxW = std::max(0, w - 2 * kPad);
  const std::string nickname = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), titleMaxW);
  renderer.drawText(UI_12_FONT_ID, x + kPad, y + kPad, nickname.c_str(), true);

  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);

  // 2. Sprite at NATIVE 96px (no upscale in the slot) at the left edge below title
  const int spriteSize = std::max(0, std::min(96, h - 2 * kPad - lineH - 4));
  const int spriteX = x + kPad;
  const int spriteY = y + kPad + lineH + 4;

  bool drewSprite = false;
  if (cachedData.valid && cachedData.speciesId > 0 && spriteSize > 0) {
    drewSprite = drawSpriteInBox(renderer, cachedData.spritePath, spriteX, spriteY, spriteSize);
  }
  if (!drewSprite && spriteSize > 0) {
    drawPokeball(renderer, spriteX + spriteSize / 2, spriteY + spriteSize / 2, spriteSize / 2 - 2);
  }

  // 3. Right column in the remaining width
  const int textX = spriteX + spriteSize + kPad;
  const int textRight = x + w - kPad;
  const int remainingWidth = std::max(0, textRight - textX);

  const int coverX = textX + std::max(0, (remainingWidth - kCoverIconSize) / 2);
  const int coverY = y + kPad + lineH + 4;

  if (remainingWidth > 0) {
    if (coverY + kCoverIconSize <= y + h) {
      drawBookIcon(renderer, book, coverX, coverY, kCoverIconSize);
    } else {
      const int shrunkSize = (y + h) - coverY;
      if (shrunkSize >= 24) {
        drawBookIcon(renderer, book, coverX, coverY, shrunkSize);
      }
    }
  }

  const int hpY = coverY + kCoverIconSize + 4;
  if (remainingWidth > 0 && hpY + kHpLabelH < y + h) {
    drawHpBar(renderer, textX, hpY, remainingWidth, cachedData.percent);
  }

  const int bottomY = y + h - kPad - smallH;
  if (remainingWidth > 0 && bottomY > hpY) {
    char levelText[16];
    std::snprintf(levelText, sizeof(levelText), "Lvl %d", cachedData.level);
    renderer.drawText(SMALL_FONT_ID, textX, bottomY, levelText, true);

    if (cachedData.valid && !cachedData.speciesName.empty()) {
      char speciesUpper[64];
      size_t len = std::min(cachedData.speciesName.size(), sizeof(speciesUpper) - 1);
      for (size_t idx = 0; idx < len; ++idx) {
        speciesUpper[idx] = static_cast<char>(std::toupper(static_cast<unsigned char>(cachedData.speciesName[idx])));
      }
      speciesUpper[len] = '\0';

      const std::string speciesText = renderer.truncatedText(SMALL_FONT_ID, speciesUpper, remainingWidth);
      const int speciesW = renderer.getTextWidth(SMALL_FONT_ID, speciesText.c_str());
      const int speciesX = std::max(textX, textRight - speciesW);
      renderer.drawText(SMALL_FONT_ID, speciesX, bottomY, speciesText.c_str(), true, EpdFontFamily::BOLD);
    }
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

void PokemonPartyTheme::invalidateCache() { g_partySlotCache.clear(); }

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
  if (buttonCount <= 0) {
    return;
  }

  const auto& menuMetrics = UITheme::getInstance().getMetrics();
  const int pad = ForkDriftMetrics::values.contentSidePadding;
  const int tileH = menuMetrics.menuRowHeight;
  const int spacing = menuMetrics.menuSpacing;
  const int areaW = rect.width - 2 * pad;
  const int tileW = (areaW - spacing * (kMenuCols - 1)) / kMenuCols;

  for (int i = 0; i < buttonCount; ++i) {
    const int col = i % kMenuCols;
    const int row = i / kMenuCols;
    const bool isLast = i == buttonCount - 1;
    const int inset = isLast ? kMenuLastInset : 0;
    const int x = rect.x + pad + col * (tileW + spacing) + inset;
    const int y = rect.y + row * (tileH + spacing);
    const int w = tileW - 2 * inset;
    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(x, y, w, tileH, kMenuCornerRadius, Color::LightGray);
    }

    const std::string label = buttonLabel(i);
    const UIIcon icon = rowIcon ? rowIcon(i) : UIIcon::Settings;
    const uint8_t* iconBmp = menuIconFor(icon);
    int textX = x + 10;
    if (iconBmp) {
      renderer.drawIcon(iconBmp, textX, y + (tileH - kMenuIconSize) / 2, kMenuIconSize, kMenuIconSize);
      textX += kMenuIconSize + kMenuHPadding;
    }
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY = y + (tileH - lineH) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label.c_str(), true);
  }
}

void PokemonPartyTheme::drawButtonHints(GfxRenderer& renderer, const char* /*btn1*/, const char* /*btn2*/,
                                        const char* /*btn3*/, const char* /*btn4*/,
                                        const bool /*allowInvertedText*/) const {
  (void)renderer;
}

#endif  // ENABLE_POKEMON_PARTY
