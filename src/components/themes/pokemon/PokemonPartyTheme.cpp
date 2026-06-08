#include "components/themes/pokemon/PokemonPartyTheme.h"

#if ENABLE_POKEMON_PARTY

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "fontIds.h"
#include "util/BookProgressDataStore.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#include "util/RecentBooksStore.h"

namespace {
constexpr int kSlots = 6;
constexpr int kSlotGap = 8;
constexpr int kPad = 8;
constexpr int kCorner = 8;
constexpr int kHpBarHeight = 12;

// "charizard" / "mr-mime" -> "Charizard" / "Mr-mime" for display.
std::string prettyName(const std::string& raw) {
  std::string out = raw;
  if (!out.empty()) {
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  }
  return out;
}

// Draw a Poké Ball outline as the offline / unconverted-sprite placeholder.
void drawPokeball(const GfxRenderer& renderer, int cx, int cy, int radius) {
  if (radius < 6) {
    return;
  }
  renderer.drawRoundedRect(cx - radius, cy - radius, radius * 2, radius * 2, 2, radius, true);
  renderer.drawLine(cx - radius, cy, cx - radius / 3, cy, 2, true);
  renderer.drawLine(cx + radius / 3, cy, cx + radius, cy, 2, true);
  const int inner = std::max(3, radius / 3);
  renderer.drawRoundedRect(cx - inner, cy - inner, inner * 2, inner * 2, 2, inner, true);
}

// Render a cached 1-bit sprite (or cover) BMP fitted into a square box.
bool drawBmpInBox(const GfxRenderer& renderer, const std::string& path, int x, int y, int size) {
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
}  // namespace

void PokemonPartyTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                            bool& /*coverRendered*/, bool& /*coverBufferStored*/,
                                            bool& /*bufferRestored*/, const std::function<bool()>& /*storeCoverBuffer*/,
                                            float /*progressPercent*/) const {
  const int margin = 12;
  const int x = rect.x + margin;
  const int w = rect.width - 2 * margin;
  const int rowH = (rect.height - kSlotGap * (kSlots - 1)) / kSlots;
  const int bookCount = static_cast<int>(recentBooks.size());

  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);

  for (int i = 0; i < kSlots; ++i) {
    const int y = rect.y + i * (rowH + kSlotGap);
    const bool selected = selectorIndex == i;

    // Empty party slot: a faint outline, like the GBA team screen's blank rows.
    if (i >= bookCount) {
      renderer.drawRoundedRect(x, y, w, rowH, 1, kCorner, true);
      continue;
    }

    renderer.drawRoundedRect(x, y, w, rowH, 2, kCorner, true);
    if (selected) {
      renderer.drawRoundedRect(x + 3, y + 3, w - 6, rowH - 6, 1, kCorner - 2, true);
    }

    const RecentBook& book = recentBooks[static_cast<size_t>(i)];

    BookProgressDataStore::ProgressData pd;
    const float percent = BookProgressDataStore::loadProgress(book.path, pd) ? pd.percent : 0.0f;
    const int level = PokemonProgress::levelForPercent(percent);
    const PokemonAssignment assignment = PokemonProgress::loadForBook(book.path);

    // --- Sprite box (left) ---
    const int spriteSz = rowH - 2 * kPad;
    const int sx = x + kPad;
    const int sy = y + kPad;
    bool drewSprite = false;
    if (assignment.valid) {
      const int speciesId = PokemonProgress::activeSpeciesId(assignment, level);
      drewSprite = drawBmpInBox(renderer, PokemonSpriteCache::spritePath(speciesId), sx, sy, spriteSz);
      if (!drewSprite) {
        drawPokeball(renderer, sx + spriteSz / 2, sy + spriteSz / 2, spriteSz / 2 - 2);
        drewSprite = true;
      }
    }
    if (!drewSprite) {
      // No Pokémon assigned: fall back to the book's cover thumbnail.
      drewSprite = drawBmpInBox(renderer, book.coverBmpPath, sx, sy, spriteSz);
      if (!drewSprite) {
        drawPokeball(renderer, sx + spriteSz / 2, sy + spriteSz / 2, spriteSz / 2 - 2);
      }
    }

    // --- Text column (right of sprite) ---
    const int tx = sx + spriteSz + kPad;
    const int textRight = x + w - kPad;

    char levelText[12];
    std::snprintf(levelText, sizeof(levelText), "Lv%d", level);
    const int levelW = renderer.getTextWidth(UI_12_FONT_ID, levelText);
    renderer.drawText(UI_12_FONT_ID, textRight - levelW, sy, levelText, true);

    // Nickname = book title, truncated to leave room for the level.
    const int nickW = std::max(0, (textRight - levelW - 6) - tx);
    const std::string nick = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), nickW);
    renderer.drawText(UI_12_FONT_ID, tx, sy, nick.c_str(), true);

    // Species name (small) under the nickname.
    if (assignment.valid && !assignment.name.empty()) {
      const std::string species =
          renderer.truncatedText(SMALL_FONT_ID, prettyName(assignment.name).c_str(), textRight - tx);
      renderer.drawText(SMALL_FONT_ID, tx, sy + lineH, species.c_str(), true);
    }

    // --- HP-style progress bar (bottom of slot) ---
    const int barY = y + rowH - kPad - kHpBarHeight;
    const char hpLabel[4] = "HP";
    const int hpLabelW = renderer.getTextWidth(SMALL_FONT_ID, hpLabel);
    renderer.drawText(SMALL_FONT_ID, tx, barY + (kHpBarHeight - smallH) / 2, hpLabel, true);
    const int barX = tx + hpLabelW + 6;
    const int barW = std::max(0, textRight - barX);
    if (barW > 4) {
      renderer.drawRect(barX, barY, barW, kHpBarHeight, true);
      const float frac = std::clamp(percent / 100.0f, 0.0f, 1.0f);
      const int fillW = static_cast<int>((barW - 2) * frac);
      if (fillW > 0) {
        renderer.fillRect(barX + 1, barY + 1, fillW, kHpBarHeight - 2, true);
      }
    }
  }
}

#endif  // ENABLE_POKEMON_PARTY
