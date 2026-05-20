#include "RecentBooksGridActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"
#include "util/BookProgressDataStore.h"
#include "util/RecentBooksStore.h"

namespace {
constexpr int kCoverCornerRadius = 2;
constexpr int kGridColumns = 3;
constexpr int kSelectionPadding = 4;
constexpr int kSelectionOuterInset = 6;

float loadRecentBookPercent(const RecentBook& book) {
  BookProgressDataStore::ProgressData progress;
  return BookProgressDataStore::loadProgress(book.path, progress) ? progress.percent : -1.0f;
}

bool hasProgressPercent(const float progress) { return progress >= 0.0f; }

void formatProgressPercent(const float progress, char* buffer, const size_t len) {
  if (!buffer || len == 0) {
    return;
  }
  if (!hasProgressPercent(progress)) {
    buffer[0] = '\0';
    return;
  }
  std::snprintf(buffer, len, "%.0f%%", std::clamp(progress, 0.0f, 100.0f));
}

void drawInlineProgressCircle(const GfxRenderer& renderer, const int x, const int y, const int size,
                              const float progressPercent) {
  const int radius = size / 2;
  if (radius <= 2) {
    return;
  }

  const int centerX = x + radius;
  const int centerY = y + radius;
  const int outerRadius = radius;
  const int innerRadius = std::max(1, radius - std::max(2, size / 4));
  const int outerRadiusSq = outerRadius * outerRadius;
  const int innerRadiusSq = innerRadius * innerRadius;
  const float sweepRadians = std::clamp(progressPercent, 0.0f, 100.0f) * 0.06283185f;

  for (int dy = -outerRadius; dy <= outerRadius; ++dy) {
    for (int dx = -outerRadius; dx <= outerRadius; ++dx) {
      const int distanceSq = dx * dx + dy * dy;
      if (distanceSq > outerRadiusSq || distanceSq < innerRadiusSq) {
        continue;
      }

      const int px = centerX + dx;
      const int py = centerY + dy;
      renderer.fillRectDither(px, py, 1, 1, Color::LightGray);

      float angle = std::atan2(static_cast<float>(dx), static_cast<float>(-dy));
      if (angle < 0.0f) {
        angle += 6.2831853f;
      }
      if (angle <= sweepRadians) {
        renderer.drawPixel(px, py, true);
      }
    }
  }
}

int moveHorizontalInGrid(const int currentIndex, const int totalItems, const bool moveRight) {
  if (totalItems <= 0) {
    return 0;
  }
  return moveRight ? ButtonNavigator::nextIndex(currentIndex, totalItems)
                   : ButtonNavigator::previousIndex(currentIndex, totalItems);
}

int moveVerticalInGrid(const int currentIndex, const int totalItems, const int columns, const int itemsPerPage,
                       const bool moveDown) {
  if (totalItems <= 0 || columns <= 0) {
    return 0;
  }

  const int safeItemsPerPage = std::max(columns, itemsPerPage);
  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  const int currentPage = currentIndex / safeItemsPerPage;
  const int indexInPage = currentIndex % safeItemsPerPage;
  const int currentRow = indexInPage / columns;
  const int currentColumn = indexInPage % columns;
  const int rowsPerPage = safeItemsPerPage / columns;

  if (moveDown) {
    if (currentRow < rowsPerPage - 1) {
      const int nextRowCandidate = currentIndex + columns;
      if (nextRowCandidate < totalItems && (nextRowCandidate / safeItemsPerPage) == currentPage) {
        return nextRowCandidate;
      }
    }

    const int nextPage = (currentPage + 1) % totalPages;
    const int nextPageStart = nextPage * safeItemsPerPage;
    const int nextPageCount = std::min(safeItemsPerPage, totalItems - nextPageStart);
    if (nextPageCount <= 0) {
      return currentIndex;
    }

    if (currentColumn < nextPageCount) {
      return nextPageStart + currentColumn;
    }
    return nextPageStart + nextPageCount - 1;
  }

  if (currentRow > 0) {
    return currentIndex - columns;
  }

  const int previousPage = (currentPage - 1 + totalPages) % totalPages;
  const int previousPageStart = previousPage * safeItemsPerPage;
  const int previousPageCount = std::min(safeItemsPerPage, totalItems - previousPageStart);
  if (previousPageCount <= 0) {
    return currentIndex;
  }

  int previousPageCandidate = previousPageStart + ((previousPageCount - 1) / columns) * columns + currentColumn;
  while (previousPageCandidate >= previousPageStart + previousPageCount) {
    previousPageCandidate -= columns;
  }
  return std::max(previousPageStart, previousPageCandidate);
}
}  // namespace

void RecentBooksGridActivity::loadRecentBooks() {
  recentBooks.clear();
  recentBookProgress.clear();
  recentBookProgressLoaded.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(books.size(), static_cast<size_t>(MAX_GRID_BOOKS)));

  for (const auto& book : books) {
    if (recentBooks.size() >= MAX_GRID_BOOKS) {
      break;
    }
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
  }

  recentBookProgress.assign(recentBooks.size(), -1.0f);
  recentBookProgressLoaded.assign(recentBooks.size(), false);
}

void RecentBooksGridActivity::ensureProgressLoaded(const int index) {
  if (index < 0 || index >= static_cast<int>(recentBooks.size())) {
    return;
  }
  if (index >= static_cast<int>(recentBookProgress.size()) ||
      index >= static_cast<int>(recentBookProgressLoaded.size()) || recentBookProgressLoaded[index]) {
    return;
  }

  recentBookProgress[index] = loadRecentBookPercent(recentBooks[index]);
  recentBookProgressLoaded[index] = true;
}

void RecentBooksGridActivity::loadPageCovers(const int pageStart) {
  const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, static_cast<int>(recentBooks.size()));

  bool needsGeneration = false;
  for (int i = pageStart; i < pageEnd; ++i) {
    if (recentBooks[i].coverBmpPath.empty()) {
      needsGeneration = true;
      break;
    }
    const std::string thumbPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, COVER_HEIGHT);
    if (!Storage.exists(thumbPath.c_str())) {
      needsGeneration = true;
      break;
    }
  }
  if (!needsGeneration) {
    loadedPageStart = pageStart;
    return;
  }

  bool showingLoading = false;
  Rect popupRect;
  const int totalToProcess = pageEnd - pageStart;
  int processedCount = 0;

  for (int i = pageStart; i < pageEnd; ++i) {
    RecentBook& book = recentBooks[i];
    const std::string coverPath =
        book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, COVER_HEIGHT);
    if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
      const auto homeCardData = core::FeatureModules::resolveHomeCardData(book.path, COVER_HEIGHT);
      if (homeCardData.handled) {
        if (!showingLoading) {
          showingLoading = true;
          popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
        }
        const int progressDenom = std::max(1, totalToProcess);
        GUI.fillPopupProgress(renderer, popupRect, 10 + processedCount * (90 / progressDenom));
        if (!homeCardData.coverPath.empty()) {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, homeCardData.coverPath);
          book.coverBmpPath = homeCardData.coverPath;
        } else if (homeCardData.loaded) {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
      }
    }
    processedCount++;
  }

  loadedPageStart = pageStart;
  if (showingLoading) {
    requestUpdate();
  }
}

void RecentBooksGridActivity::onEnter() {
  Activity::onEnter();
  loadRecentBooks();
  selectorIndex = 0;
  loadedPageStart = -1;
  ensureProgressLoaded(0);
  requestUpdate();
}

void RecentBooksGridActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  recentBookProgress.clear();
  recentBookProgressLoaded.clear();
}

void RecentBooksGridActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < recentBooks.size()) {
      LOG_DBG("RBGA", "Selected recent book: %s", recentBooks[selectorIndex].path.c_str());
      activityManager.goToReader(recentBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  const int listSize = static_cast<int>(recentBooks.size());

  buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this, listSize] {
    selectorIndex = moveHorizontalInGrid(selectorIndex, listSize, true);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this, listSize] {
    selectorIndex = moveHorizontalInGrid(selectorIndex, listSize, false);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, listSize] {
    selectorIndex = moveVerticalInGrid(selectorIndex, listSize, kGridColumns, BOOKS_PER_PAGE, true);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, listSize] {
    selectorIndex = moveVerticalInGrid(selectorIndex, listSize, kGridColumns, BOOKS_PER_PAGE, false);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });

  buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, listSize] {
    selectorIndex = moveHorizontalInGrid(selectorIndex, listSize, true);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, listSize] {
    selectorIndex = moveHorizontalInGrid(selectorIndex, listSize, false);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, listSize] {
    selectorIndex = moveVerticalInGrid(selectorIndex, listSize, kGridColumns, BOOKS_PER_PAGE, true);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, listSize] {
    selectorIndex = moveVerticalInGrid(selectorIndex, listSize, kGridColumns, BOOKS_PER_PAGE, false);
    ensureProgressLoaded(selectorIndex);
    requestUpdate();
  });
}

void RecentBooksGridActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  constexpr int titleStripHeight = 32;
  constexpr int titleGridGap = 16;
  const int rowSpacing = metrics.verticalSpacing + 4;
  const int totalGridWidth = kGridColumns * COVER_WIDTH + (kGridColumns - 1) * metrics.verticalSpacing;
  const int startXOffset = (pageWidth - totalGridWidth) / 2;

  const int totalBooks = static_cast<int>(recentBooks.size());
  const int totalPages = (totalBooks + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
  const int currentPage = totalBooks > 0 ? selectorIndex / BOOKS_PER_PAGE : 0;
  const int pageStart = currentPage * BOOKS_PER_PAGE;
  const int pageCount = std::min(BOOKS_PER_PAGE, totalBooks - pageStart);

  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    if (selectorIndex < recentBooks.size()) {
      const int titleLh = renderer.getLineHeight(UI_10_FONT_ID);
      const int titleY = contentTop + (titleStripHeight - titleLh) / 2;
      char progressLabel[8];
      progressLabel[0] = '\0';
      const bool hasProgress = selectorIndex < recentBookProgress.size() && selectorIndex < recentBookProgressLoaded.size() &&
                               recentBookProgressLoaded[selectorIndex] &&
                               hasProgressPercent(recentBookProgress[selectorIndex]);
      if (hasProgress) {
        formatProgressPercent(recentBookProgress[selectorIndex], progressLabel, sizeof(progressLabel));
      }

      const int progressIconSize = hasProgress ? std::max(8, titleLh - 2) : 0;
      const char* progressSeparator = "  |  ";
      const int separatorWidth =
          hasProgress ? renderer.getTextWidth(UI_10_FONT_ID, progressSeparator, EpdFontFamily::REGULAR) : 0;
      const int progressWidth =
          hasProgress ? renderer.getTextWidth(UI_10_FONT_ID, progressLabel, EpdFontFamily::REGULAR) : 0;
      const int progressIconGap = hasProgress ? renderer.getTextWidth(UI_10_FONT_ID, "  ", EpdFontFamily::REGULAR) : 0;
      const int progressSuffixWidth =
          hasProgress ? separatorWidth + progressWidth + progressIconGap + progressIconSize : 0;
      const int titleMaxWidth = std::max(0, totalGridWidth - progressSuffixWidth);
      const std::string truncTitle = renderer.truncatedText(UI_10_FONT_ID, recentBooks[selectorIndex].title.c_str(),
                                                            titleMaxWidth, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, startXOffset, titleY, truncTitle.c_str(), true, EpdFontFamily::REGULAR);
      if (hasProgress) {
        const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, truncTitle.c_str(), EpdFontFamily::REGULAR);
        int progressX = startXOffset + titleWidth;
        progressX = std::min(progressX, startXOffset + totalGridWidth - progressSuffixWidth);
        renderer.drawText(UI_10_FONT_ID, progressX, titleY, progressSeparator, true, EpdFontFamily::REGULAR);
        progressX += separatorWidth;
        renderer.drawText(UI_10_FONT_ID, progressX, titleY, progressLabel, true, EpdFontFamily::REGULAR);
        const int iconX = progressX + progressWidth + progressIconGap;
        const int iconY = titleY + (titleLh - progressIconSize) / 2;
        drawInlineProgressCircle(renderer, iconX, iconY, progressIconSize, recentBookProgress[selectorIndex]);
      }
    }

    for (int i = 0; i < pageCount; ++i) {
      const int bookIdx = pageStart + i;
      const int col = i % kGridColumns;
      const int row = i / kGridColumns;
      const int x = startXOffset + col * (COVER_WIDTH + metrics.verticalSpacing);
      const int y = contentTop + titleStripHeight + titleGridGap + row * (COVER_HEIGHT + rowSpacing);

      int bx = x;
      int by = y;
      int bw = COVER_WIDTH;
      int bh = COVER_HEIGHT;
      bool drawn = false;
      const std::string thumbPath = recentBooks[bookIdx].coverBmpPath.empty()
                                        ? ""
                                        : UITheme::getCoverThumbPath(recentBooks[bookIdx].coverBmpPath, COVER_HEIGHT);
      if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
        FsFile file;
        if (Storage.openFileForRead("RBGA", thumbPath, file)) {
          Bitmap bmp(file);
          if (bmp.parseHeaders() == BmpReaderError::Ok && bmp.getWidth() > 0 && bmp.getHeight() > 0) {
            bw = std::min(COVER_WIDTH, bmp.getWidth());
            bh = std::min(COVER_HEIGHT, bmp.getHeight());
            bx = x + (COVER_WIDTH - bw) / 2;
            by = y + (COVER_HEIGHT - bh) / 2;
            renderer.drawBitmap(bmp, bx, by, bw, bh);
            renderer.maskRoundedRectOutsideCorners(bx, by, bw, bh, kCoverCornerRadius, Color::White);
            renderer.drawRoundedRect(bx, by, bw, bh, 2, kCoverCornerRadius, true);
            drawn = true;
          }
          file.close();
        }
      }
      if (!drawn) {
        renderer.fillRoundedRect(bx, by, bw, bh, kCoverCornerRadius, Color::White);
        renderer.drawRoundedRect(bx, by, bw, bh, 2, kCoverCornerRadius, true);
        renderer.drawIcon(BookIcon, bx + (bw - 32) / 2, by + (bh - 32) / 2, 32, 32);
      }
      if (bookIdx == selectorIndex) {
        renderer.drawRoundedRect(bx - kSelectionPadding, by - kSelectionPadding, bw + kSelectionPadding * 2,
                                 bh + kSelectionPadding * 2, 3, kCoverCornerRadius + kSelectionPadding, true);
        renderer.drawRoundedRect(bx - kSelectionOuterInset, by - kSelectionOuterInset, bw + kSelectionOuterInset * 2,
                                 bh + kSelectionOuterInset * 2, 1, kCoverCornerRadius + kSelectionOuterInset, true);
      }
    }

    if (totalPages > 1) {
      constexpr int dotSize = 8;
      constexpr int dotSpacing = 6;
      const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
      const int dotsStartX = (pageWidth - totalDotWidth) / 2;
      const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;
      for (int p = 0; p < totalPages; ++p) {
        const int dx = dotsStartX + p * (dotSize + dotSpacing);
        if (p == currentPage) {
          renderer.fillRect(dx, dotY, dotSize, dotSize, true);
        } else {
          renderer.drawRect(dx, dotY, dotSize, dotSize, true);
        }
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!recentBooks.empty() && loadedPageStart != pageStart) {
    loadPageCovers(pageStart);
  }
}
