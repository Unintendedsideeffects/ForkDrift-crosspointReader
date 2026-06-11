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
constexpr int kStripeStep = 8;
constexpr int kFractionGap = 3;

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

void drawFeaturedSlot(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                      const RecentBook& book, const bool selected) {
  renderer.fillRoundedRect(x, y, w, h, kCorner, Color::LightGray);
  renderer.drawRoundedRect(x, y, w, h, 2, kCorner, true);

  const CachedSlotData& data = slotDataFor(book);
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  const int spriteSize = std::max(0, std::min(96, h - 2 * kPad));
  const int spriteX = x + kPad;
  const int spriteY = y + (h - spriteSize) / 2;
  drawPokemonOrBook(renderer, book, data, spriteX, spriteY, spriteSize);

  const int textX = spriteX + spriteSize + kPad;
  const int right = x + w - kPad;
  const int textW = std::max(0, right - textX);
  if (textW > 0) {
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), textW);
    renderer.drawText(UI_12_FONT_ID, textX, y + kPad, title.c_str(), true, EpdFontFamily::BOLD);

    char levelText[16];
    std::snprintf(levelText, sizeof(levelText), "Lv %d", data.level);
    renderer.drawText(SMALL_FONT_ID, textX, y + kPad + titleH + 2, levelText, true);

    const int fractionY = y + h - kPad - smallH;
    const int hpY = fractionY - kFractionGap - kHpLabelH;
    drawHpBar(renderer, textX, hpY, textW, data.hpPercent);
    drawFraction(renderer, data, right, fractionY);
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

  if (leftW > 0) {
    const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), leftW);
    renderer.drawText(UI_10_FONT_ID, textX, y + kPad, title.c_str(), true, EpdFontFamily::BOLD);
    char levelText[16];
    std::snprintf(levelText, sizeof(levelText), "Lv %d", data.level);
    renderer.drawText(SMALL_FONT_ID, textX, y + h - kPad - smallH, levelText, true);
  }

  if (hpW > 0) {
    const int fractionY = y + h - kPad - smallH;
    const int hpY = std::max(y + kPad, fractionY - kFractionGap - kHpLabelH);
    drawHpBar(renderer, hpX, hpY, hpW, data.hpPercent);
    drawFraction(renderer, data, right, fractionY);
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
  drawPartyBackground(renderer, rect);

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), kSlots);
  if (bookCount <= 0) {
    return;
  }

  const int areaW = rect.width - 2 * kMargin;
  const int areaH = rect.height - 2 * kMargin;
  const int rowsAreaH = std::max(0, areaH - kGap * kCompactSlots);
  const int rowH = rowsAreaH / (kCompactSlots + 2);
  const int featuredH = std::max(0, areaH - kCompactSlots * (rowH + kGap));
  const int x = rect.x + kMargin;
  const int featuredY = rect.y + kMargin;

  drawFeaturedSlot(renderer, x, featuredY, areaW, featuredH, recentBooks[0], selectorIndex == 0);

  for (int i = 1; i < bookCount; ++i) {
    const int rowY = featuredY + featuredH + kGap + (i - 1) * (rowH + kGap);
    drawCompactSlot(renderer, x, rowY, areaW, rowH, recentBooks[static_cast<size_t>(i)], selectorIndex == i);
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
#endif  // ENABLE_POKEMON_PARTY
