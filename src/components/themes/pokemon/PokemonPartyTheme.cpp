#include "components/themes/pokemon/PokemonPartyTheme.h"

#if ENABLE_POKEMON_PARTY

#include <Bitmap.h>
#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>
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
constexpr int kSlots = 6;
constexpr int kCompactSlots = kSlots - 1;

struct CachedSlotData {
  bool valid = false;
  float percent = 0.0f;
  float hpPercent = 0.0f;
  uint32_t positionCurrent = 0;
  uint32_t positionTotal = 0;
  int level = 1;
  int speciesId = 0;
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
constexpr int kFractionGap = 3;

constexpr int kMenuIconBox = 32;  // largest icon; the strip layout centers smaller ones
constexpr int kMenuCornerRadius = 6;
constexpr int kMenuIconPad = 6;
constexpr int kMenuHighlightPad = 5;
constexpr int kMenuLabelGap = 4;

// Icon bitmaps come in two native sizes; drawing one with the wrong stride
// produces noise, so each entry carries its real dimensions.
struct MenuIcon {
  const uint8_t* bmp = nullptr;
  int size = 0;
};

MenuIcon menuIconFor(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return {FolderIcon, 32};
    case UIIcon::Settings:
      return {Settings2Icon, 32};
    case UIIcon::Transfer:
      return {TransferIcon, 32};
    case UIIcon::Calendar:
      return {CalendarIcon, 32};
    case UIIcon::Text:
      return {Text24Icon, 24};
    case UIIcon::Book:
      return {Book24Icon, 24};
    default:
      return {};
  }
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
  if (!book.coverBmpPath.empty()) {
    const std::string squarePath = UITheme::getCoverThumbPath(book.coverBmpPath, PokemonPartyTheme::kCoverIconSize,
                                                              PokemonPartyTheme::kCoverIconSize);
    if (!squarePath.empty() && Storage.exists(squarePath.c_str())) {
      if (drawBmpInBox(renderer, squarePath, x, y, size)) {
        return;
      }
    }
    if (drawBmpInBox(renderer, book.coverBmpPath, x, y, size)) {
      return;
    }
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

void drawSelectionRing(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.drawRoundedRect(x - 2, y - 2, w + 4, h + 4, kSelectBorder, kCorner + 2, true);
  renderer.drawRoundedRect(x + 2, y + 2, w - 4, h - 4, 2, std::max(1, kCorner - 2), true);
}

void drawFraction(const GfxRenderer& renderer, const CachedSlotData& data, const int right, const int y) {
  if (data.positionTotal == 0) {
    return;
  }
  char fraction[32];
  std::snprintf(fraction, sizeof(fraction), "%lu/%lu", static_cast<unsigned long>(data.positionCurrent),
                static_cast<unsigned long>(data.positionTotal));
  renderer.drawText(SMALL_FONT_ID, right - renderer.getTextWidth(SMALL_FONT_ID, fraction), y, fraction, true);
}

CachedSlotData loadSlotData(const RecentBook& book) {
  CachedSlotData data;
  BookProgressDataStore::ProgressData progress;
  const bool hasProgress = BookProgressDataStore::loadProgress(book.path, progress);
  data.percent = hasProgress ? progress.percent : 0.0f;
  data.hpPercent = data.percent;
  data.level = PokemonProgress::levelForPercent(data.percent);

  if (hasProgress && progress.kind == BookProgressDataStore::BookKind::Epub && progress.spineIndex >= 0) {
    std::string cachePath;
    if (BookProgressDataStore::resolveCachePath(book.path, cachePath)) {
      std::unique_ptr<BookMetadataCache> metadata(new (std::nothrow) BookMetadataCache(cachePath));
      if (metadata && metadata->load() && metadata->getSpineCount() > 0) {
        data.positionTotal = static_cast<uint32_t>(metadata->getSpineCount());
        data.positionCurrent =
            std::clamp(static_cast<uint32_t>(progress.spineIndex + 1), uint32_t{1}, data.positionTotal);
      }
    }
  } else if (hasProgress && progress.pageCount > 0) {
    data.positionTotal = progress.pageCount;
    data.positionCurrent = std::clamp(progress.page, uint32_t{1}, progress.pageCount);
  }

  if (data.positionTotal > 0) {
    data.hpPercent = static_cast<float>(data.positionCurrent) * 100.0f / static_cast<float>(data.positionTotal);
  }

  const PokemonAssignment assignment = PokemonProgress::loadForBook(book.path);
  data.valid = assignment.valid;
  data.speciesId = assignment.valid ? PokemonProgress::activeSpeciesId(assignment, data.level) : 0;
  if (data.speciesId > 0) {
    data.spritePath = PokemonSpriteCache::spritePath(data.speciesId);
  }
  return data;
}

const CachedSlotData& slotDataFor(const RecentBook& book) {
  const auto cached = g_partySlotCache.find(book.path);
  if (cached != g_partySlotCache.end()) {
    return cached->second;
  }
  return g_partySlotCache.emplace(book.path, loadSlotData(book)).first->second;
}

void drawPokemonOrBook(const GfxRenderer& renderer, const RecentBook& book, const CachedSlotData& data, const int x,
                       const int y, const int size) {
  if (size <= 0) {
    return;
  }
  if (data.valid && data.speciesId > 0 && drawSpriteInBox(renderer, data.spritePath, x, y, size)) {
    return;
  }
  drawBookIcon(renderer, book, x, y, size);
}

struct FeaturedSlotLayout {
  int spriteX;
  int spriteY;
  int spriteSize;
  int textX;
  int titleY;
  int titleW;
  int levelY;
  int hpX;
  int hpY;
  int hpW;
  int fractionRight;
  int fractionY;
};

FeaturedSlotLayout featuredSlotLayout(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const bool stacked = h > w;

  if (stacked) {
    const int textX = x + kPad;
    const int right = x + w - kPad;
    const int textW = std::max(0, right - textX);
    const int fractionY = y + h - kPad - smallH;
    const int hpY = fractionY - kFractionGap - kHpLabelH;
    const int levelY = hpY - kFractionGap - smallH;
    const int titleY = levelY - 2 - titleH;
    const int spriteMaxH = std::max(0, titleY - kPad - (y + kPad));
    const int spriteSize = std::max(0, std::min({96, w - 2 * kPad, spriteMaxH}));
    return {x + (w - spriteSize) / 2,
            y + kPad,
            spriteSize,
            textX,
            titleY,
            textW,
            levelY,
            textX,
            hpY,
            textW,
            right,
            fractionY};
  }

  const int spriteSize = std::max(0, std::min(96, h - 2 * kPad));
  const int spriteX = x + kPad;
  const int textX = spriteX + spriteSize + kPad;
  const int right = x + w - kPad;
  const int textW = std::max(0, right - textX);
  const int fractionY = y + h - kPad - smallH;
  const int hpY = fractionY - kFractionGap - kHpLabelH;
  return {spriteX,
          y + (h - spriteSize) / 2,
          spriteSize,
          textX,
          y + kPad,
          textW,
          y + kPad + titleH + 2,
          textX,
          hpY,
          textW,
          right,
          fractionY};
}

void drawFeaturedSlot(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                      const RecentBook& book, const bool selected) {
  renderer.fillRoundedRect(x, y, w, h, kCorner, Color::LightGray);
  renderer.drawRoundedRect(x, y, w, h, 2, kCorner, true);

  const CachedSlotData& data = slotDataFor(book);
  const FeaturedSlotLayout layout = featuredSlotLayout(renderer, x, y, w, h);
  drawPokemonOrBook(renderer, book, data, layout.spriteX, layout.spriteY, layout.spriteSize);

  if (layout.titleW > 0) {
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), layout.titleW);
    renderer.drawText(UI_12_FONT_ID, layout.textX, layout.titleY, title.c_str(), true, EpdFontFamily::BOLD);

    char levelText[16];
    std::snprintf(levelText, sizeof(levelText), "Lv %d", data.level);
    renderer.drawText(SMALL_FONT_ID, layout.textX, layout.levelY, levelText, true);

    drawHpBar(renderer, layout.hpX, layout.hpY, layout.hpW, data.hpPercent);
    drawFraction(renderer, data, layout.fractionRight, layout.fractionY);
  }

  if (selected) {
    drawSelectionRing(renderer, x, y, w, h);
  }
}

void drawCompactSlot(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                     const RecentBook& book, const bool selected) {
  renderer.fillRoundedRect(x, y, w, h, kCorner, Color::LightGray);
  renderer.drawRoundedRect(x, y, w, h, 2, kCorner, true);

  const CachedSlotData& data = slotDataFor(book);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const int spriteSize = std::max(0, h - 2 * kPad);
  const int spriteX = x + kPad;
  const int spriteY = y + kPad;
  drawPokemonOrBook(renderer, book, data, spriteX, spriteY, spriteSize);

  const int textX = spriteX + spriteSize + kPad;
  const int right = x + w - kPad;
  const int contentW = std::max(0, right - textX);
  const int leftW = contentW * 2 / 5;
  const int hpX = textX + leftW + kPad;
  const int hpW = std::max(0, right - hpX);

  // Landscape rows are too short to stack title over level; in that "tight"
  // mode everything shares one vertically-centered line and the fraction sits
  // inline after a shortened HP bar instead of beneath it.
  const int titleH = renderer.getLineHeight(UI_8_FONT_ID);
  const bool tight = h < 2 * kPad + titleH + smallH;

  char levelText[16];
  std::snprintf(levelText, sizeof(levelText), "Lv %d", data.level);

  if (leftW > 0) {
    if (tight) {
      const int levelW = renderer.getTextWidth(SMALL_FONT_ID, levelText);
      const int titleW = std::max(0, leftW - levelW - kPad);
      const std::string title = renderer.truncatedText(UI_8_FONT_ID, book.title.c_str(), titleW);
      renderer.drawText(UI_8_FONT_ID, textX, y + (h - titleH) / 2, title.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, textX + leftW - levelW, y + (h - smallH) / 2, levelText, true);
    } else {
      // Non-tight: title spans the full content width on its own top line; the
      // bottom line carries the Lv label on the left and the HP bar + fraction
      // on the right (one info row, so it never collides with the full-width
      // title above it).
      const std::string title = renderer.truncatedText(UI_8_FONT_ID, book.title.c_str(), contentW);
      renderer.drawText(UI_8_FONT_ID, textX, y + kPad, title.c_str(), true, EpdFontFamily::BOLD);

      const int infoH = std::max(smallH, kHpLabelH);
      const int infoY = y + h - kPad - infoH;
      const int levelW = renderer.getTextWidth(SMALL_FONT_ID, levelText);
      renderer.drawText(SMALL_FONT_ID, textX, infoY + (infoH - smallH) / 2, levelText, true);

      int fractionW = 0;
      if (data.positionTotal > 0) {
        char fraction[32];
        std::snprintf(fraction, sizeof(fraction), "%lu/%lu", static_cast<unsigned long>(data.positionCurrent),
                      static_cast<unsigned long>(data.positionTotal));
        fractionW = renderer.getTextWidth(SMALL_FONT_ID, fraction) + kPad;
      }
      const int hpLineX = textX + levelW + kPad;
      const int hpLineW = std::max(0, right - hpLineX - fractionW);
      drawHpBar(renderer, hpLineX, infoY + (infoH - kHpLabelH) / 2, hpLineW, data.hpPercent);
      drawFraction(renderer, data, right, infoY + (infoH - smallH) / 2);
    }
  }

  if (hpW > 0) {
    if (tight) {
      int fractionW = 0;
      if (data.positionTotal > 0) {
        char fraction[32];
        std::snprintf(fraction, sizeof(fraction), "%lu/%lu", static_cast<unsigned long>(data.positionCurrent),
                      static_cast<unsigned long>(data.positionTotal));
        fractionW = renderer.getTextWidth(SMALL_FONT_ID, fraction) + kPad;
      }
      drawHpBar(renderer, hpX, y + (h - kHpLabelH) / 2, std::max(0, hpW - fractionW), data.hpPercent);
      drawFraction(renderer, data, right, y + (h - smallH) / 2);
    } else {
      // HP bar and fraction are now drawn in the non-tight text block above.
    }
  }

  if (selected) {
    drawSelectionRing(renderer, x, y, w, h);
  }
}
}  // namespace

void PokemonPartyTheme::invalidateCache() { g_partySlotCache.clear(); }

void PokemonPartyTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                            bool& /*coverRendered*/, bool& /*coverBufferStored*/,
                                            bool& /*bufferRestored*/, const std::function<bool()>& /*storeCoverBuffer*/,
                                            float /*progressPercent*/) const {
  const int bookCount = std::min(static_cast<int>(recentBooks.size()), kSlots);
  if (bookCount <= 0) {
    return;
  }

  const int areaW = rect.width - 2 * kMargin;
  const int areaH = rect.height - 2 * kMargin;
  const int x = rect.x + kMargin;
  const int y = rect.y + kMargin;

  if (rect.width > rect.height) {
    const int featuredW = areaW * 2 / 5;
    const int rowsX = x + featuredW + kGap;
    const int rowsW = std::max(0, areaW - featuredW - kGap);
    const int rowH = std::max(0, areaH - kGap * (kCompactSlots - 1)) / kCompactSlots;

    drawFeaturedSlot(renderer, x, y, featuredW, areaH, recentBooks[0], selectorIndex == 0);

    for (int i = 1; i < bookCount; ++i) {
      const int rowY = y + (i - 1) * (rowH + kGap);
      drawCompactSlot(renderer, rowsX, rowY, rowsW, rowH, recentBooks[static_cast<size_t>(i)], selectorIndex == i);
    }
    return;
  }

  const int rowsAreaH = std::max(0, areaH - kGap * kCompactSlots);
  const int rowH = rowsAreaH / (kCompactSlots + 2);
  const int featuredH = std::max(0, areaH - kCompactSlots * (rowH + kGap));

  drawFeaturedSlot(renderer, x, y, areaW, featuredH, recentBooks[0], selectorIndex == 0);

  for (int i = 1; i < bookCount; ++i) {
    const int rowY = y + featuredH + kGap + (i - 1) * (rowH + kGap);
    drawCompactSlot(renderer, x, rowY, areaW, rowH, recentBooks[static_cast<size_t>(i)], selectorIndex == i);
  }
}

void PokemonPartyTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                       const std::function<std::string(int index)>& buttonLabel,
                                       const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) {
    return;
  }

  // Carousel-style strip: one row of evenly spaced icons with the selected
  // action's label centered beneath. The party area above gets the rest of
  // the screen.
  const int labelH = renderer.getLineHeight(UI_10_FONT_ID);
  const int iconRowH = kMenuIconBox + 2 * kMenuIconPad;
  const int stripH = iconRowH + kMenuLabelGap + labelH;
  const int rowY = rect.y + std::max(0, (rect.height - stripH) / 2);
  const int tileW = rect.width / buttonCount;

  for (int i = 0; i < buttonCount; ++i) {
    const int boxX = rect.x + i * tileW + (tileW - kMenuIconBox) / 2;
    const int boxY = rowY + kMenuIconPad;

    if (selectedIndex == i) {
      const int highlight = kMenuIconBox + 2 * kMenuHighlightPad;
      renderer.fillRoundedRect(boxX - kMenuHighlightPad, boxY - kMenuHighlightPad, highlight, highlight,
                               kMenuCornerRadius, Color::LightGray);
    }

    const MenuIcon icon = menuIconFor(rowIcon ? rowIcon(i) : UIIcon::Settings);
    if (icon.bmp != nullptr && icon.size > 0) {
      const int inset = (kMenuIconBox - icon.size) / 2;
      renderer.drawIcon(icon.bmp, boxX + inset, boxY + inset, icon.size, icon.size);
    }
  }

  if (selectedIndex >= 0 && selectedIndex < buttonCount && buttonLabel) {
    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, buttonLabel(selectedIndex).c_str(), rect.width - 40);
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    renderer.drawText(UI_10_FONT_ID, rect.x + (rect.width - labelW) / 2, rowY + iconRowH + kMenuLabelGap, label.c_str(),
                      true);
  }
}
#endif  // ENABLE_POKEMON_PARTY
