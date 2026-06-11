#include "RecentBooksActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#include "util/RecentBooksStore.h"
#include "util/StringUtils.h"

namespace {
constexpr int kPartyMaxBooks = 6;

std::string fallbackTitleFromPath(const std::string& path) {
  auto title = path;
  const size_t lastSlash = title.find_last_of('/');
  if (lastSlash != std::string::npos) {
    title = title.substr(lastSlash + 1);
  }

  if (FsHelpers::checkFileExtension(title, ".xtch")) {
    title.resize(title.length() - 5);
  } else if (FsHelpers::checkFileExtension(title, ".epub") || FsHelpers::checkFileExtension(title, ".xtc") ||
             FsHelpers::checkFileExtension(title, ".txt") || FsHelpers::checkFileExtension(title, ".md")) {
    title.resize(title.length() - 4);
  }

  return title;
}

std::string titleCase(const std::string& value) {
  std::string result = value;
  bool capitalize = true;
  for (char& c : result) {
    if (c == '-' || c == '_' || c == ' ') {
      if (c == '_' || c == '-') {
        c = ' ';
      }
      capitalize = true;
      continue;
    }
    const unsigned char uc = static_cast<unsigned char>(c);
    c = capitalize ? static_cast<char>(std::toupper(uc)) : static_cast<char>(std::tolower(uc));
    capitalize = false;
  }
  return result;
}

}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  partyMode = (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::POKEMON_PARTY);
  const auto& books = RECENT_BOOKS.getBooks();
  const size_t limit = partyMode ? std::min(static_cast<size_t>(kPartyMaxBooks), books.size()) : books.size();
  recentBooks.reserve(limit);

  for (const auto& book : books) {
    if (recentBooks.size() >= limit) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    PartyBookEntry entry;
    entry.book = book;
    if (entry.book.title.empty()) {
      entry.book.title = fallbackTitleFromPath(entry.book.path);
    }

    if (partyMode) {
      // Resolve level + species through the SAME path the party home screen
      // uses (PokemonProgress), so both screens always show the same Pokémon
      // at the same evolution stage.
      entry.hasProgress = BookProgressDataStore::loadProgress(entry.book.path, entry.progress);
      const float percent = entry.hasProgress ? entry.progress.percent : 0.0f;
      entry.level = PokemonProgress::levelForPercent(percent);
      entry.progressLabel = std::string(tr(STR_PARTY_LV)) + " " + std::to_string(entry.level);

      const PokemonAssignment assignment = PokemonProgress::loadForBook(entry.book.path);
      if (assignment.valid) {
        const int stage = PokemonProgress::activeStageIndex(assignment, entry.level);
        const int speciesId = PokemonProgress::activeSpeciesId(assignment, entry.level);
        const std::string& stageName = (!assignment.chain.empty() && stage < static_cast<int>(assignment.chain.size()))
                                           ? assignment.chain[static_cast<size_t>(stage)].name
                                           : assignment.name;
        entry.pokemonLabel = titleCase(stageName);
        if (speciesId > 0) {
          std::string spritePath = PokemonSpriteCache::spritePath(speciesId);
          if (Storage.exists(spritePath.c_str())) {
            entry.spritePath = std::move(spritePath);
          }
        }
      }
      // No assignment: show the book cover and no made-up Pokémon name.
    }

    recentBooks.push_back(std::move(entry));
  }
}

bool RecentBooksActivity::drawSpriteAt(const std::string& spritePath, const int x, const int y, const int size) const {
  if (spritePath.empty() || !Storage.exists(spritePath.c_str())) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("PTY", spritePath, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.is1Bit();
  if (ok) {
    renderer.drawBitmap1Bit(bitmap, x, y, size, size);
  }
  file.close();
  return ok;
}

bool RecentBooksActivity::drawCoverAt(const std::string& coverPath, const int x, const int y, const int width,
                                      const int height) const {
  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("PTY", coverPath, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    renderer.drawBitmap(bitmap, x, y, width, height);
  }
  file.close();
  return ok;
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadRecentBooks();

  selectorIndex = 0;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
}

void RecentBooksActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < recentBooks.size()) {
      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].book.path.c_str());
      activityManager.goToReader(recentBooks[selectorIndex].book.path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }

  int listSize = static_cast<int>(recentBooks.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 partyMode ? tr(STR_PARTY) : tr(STR_MENU_RECENT_BOOKS), partyMode ? tr(STR_PARTY_SUBTITLE) : nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20,
                      partyMode ? tr(STR_PARTY_EMPTY) : tr(STR_NO_RECENT_BOOKS));
  } else if (partyMode) {
    const int slotCount = static_cast<int>(recentBooks.size());
    const int slotGap = 8;
    const int slotHeight = (contentHeight - slotGap * std::max(0, slotCount - 1)) / std::max(1, slotCount);
    const int slotWidth = pageWidth - metrics.contentSidePadding * 2;
    const int coverHeight = std::max(56, slotHeight - 18);
    const int coverWidth = coverHeight * 3 / 4;

    for (int i = 0; i < slotCount; ++i) {
      const auto& entry = recentBooks[static_cast<size_t>(i)];
      const bool selected = static_cast<int>(selectorIndex) == i;
      const int y = contentTop + i * (slotHeight + slotGap);
      const int x = metrics.contentSidePadding;
      const int textX = x + 12 + coverWidth + 12;
      const int textW = slotWidth - (textX - x) - 12;
      const int titleY = y + 8;
      const int lineH = renderer.getLineHeight(UI_10_FONT_ID);

      if (selected) {
        renderer.fillRect(x, y, slotWidth, slotHeight);
      } else {
        renderer.drawRect(x, y, slotWidth, slotHeight);
      }

      bool drewVisual = false;
      if (!entry.spritePath.empty()) {
        // 1-bit sprites draw black-only (white = transparent), so give them a
        // white backdrop on selected (inverted) rows to stay visible.
        if (selected) {
          renderer.fillRect(x + 8, y + 8, coverWidth, coverHeight, false);
        }
        const int spriteBox = std::min(coverWidth, coverHeight);
        drewVisual = drawSpriteAt(entry.spritePath, x + 8 + (coverWidth - spriteBox) / 2,
                                  y + 8 + (coverHeight - spriteBox) / 2, spriteBox);
      }
      if (!drewVisual) {
        const std::string thumbPath = UITheme::getCoverThumbPath(entry.book.coverBmpPath, coverHeight);
        drewVisual = drawCoverAt(thumbPath, x + 8, y + 8, coverWidth, coverHeight);
      }
      if (!drewVisual) {
        renderer.drawRect(x + 8, y + 8, coverWidth, coverHeight, !selected);
        const int placeholderY = y + 8 + (coverHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
        const int placeholderX = x + 8 + std::max(0, (coverWidth - renderer.getTextWidth(UI_10_FONT_ID, "BOOK")) / 2);
        renderer.drawText(UI_10_FONT_ID, placeholderX, placeholderY, "BOOK", !selected);
      }

      renderer.drawText(UI_12_FONT_ID, textX, titleY,
                        renderer.truncatedText(UI_12_FONT_ID, entry.book.title.c_str(), textW).c_str(), !selected);
      if (!entry.pokemonLabel.empty()) {
        renderer.drawText(UI_10_FONT_ID, textX, titleY + lineH + 2,
                          renderer.truncatedText(UI_10_FONT_ID, entry.pokemonLabel.c_str(), textW).c_str(), !selected);
      }
      renderer.drawText(UI_10_FONT_ID, textX, titleY + lineH * 2 + 4,
                        renderer.truncatedText(UI_10_FONT_ID, entry.progressLabel.c_str(), textW).c_str(), !selected);
    }
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, recentBooks.size(), selectorIndex,
        [this](int index) { return recentBooks[index].book.title; },
        [this](int index) { return recentBooks[index].book.author; },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].book.path); });
  }

  // Help text
  const auto labels = mappedInput.mapLabels("« Home", "Open", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
