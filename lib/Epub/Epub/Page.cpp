#include "Page.h"

#include <GfxRenderer.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <Serialization.h>

#include "CacheLoadStatus.h"

namespace {

bool g_outOfMemoryFlag = false;

constexpr uint16_t MAX_PAGE_ELEMENTS = 1024;
constexpr uint8_t MAX_TABLE_ROWS_PER_FRAGMENT = 64;
constexpr uint8_t MAX_TABLE_CELLS_PER_ROW = 8;
constexpr uint8_t MAX_TABLE_LINES_PER_CELL = 64;

bool canAllocateElements(const size_t count, const size_t elementSize) {
  return count == 0 || (count <= SIZE_MAX / elementSize && heapguard::canAllocate(count * elementSize, 0));
}

template <typename Predicate>
void renderFilteredPageElements(const std::vector<std::shared_ptr<PageElement>>& elements, GfxRenderer& renderer,
                                const int fontId, const int xOffset, const int yOffset, Predicate&& predicate) {
  for (const auto& element : elements) {
    if (predicate(*element)) {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

}  // namespace

namespace cacheload {
void beginLoad() { g_outOfMemoryFlag = false; }
void markOutOfMemory() { g_outOfMemoryFlag = true; }
bool wasOutOfMemory() { return g_outOfMemoryFlag; }
}  // namespace cacheload

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(serialization::BufferedWriter& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(serialization::BufferedReader& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos)) {
    LOG_ERR("PGE", "Deserialization failed: truncated PageLine coordinates");
    return nullptr;
  }

  auto tb = TextBlock::deserialize(file);
  if (!tb) {
    // TextBlock failed (OOM or corrupt). PageLine::render dereferences block
    // unconditionally, so a null block must never reach a constructed PageLine.
    LOG_ERR("PGE", "Deserialization failed: null TextBlock for PageLine");
    return nullptr;
  }
  auto pl = std::unique_ptr<PageLine>(new (std::nothrow) PageLine(std::move(tb), xPos, yPos));
  if (!pl) {
    LOG_ERR("PGE", "OOM: PageLine");
    cacheload::markOutOfMemory();
  }
  return pl;
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (!imageBlock) {
    return;
  }
  if (renderer.getRenderMode() != GfxRenderer::BW) {
    return;
  }
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(serialization::BufferedWriter& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  return imageBlock && imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(serialization::BufferedReader& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos)) {
    LOG_ERR("PGE", "Deserialization failed: truncated PageImage coordinates");
    return nullptr;
  }

  auto imageBlockUnique = ImageBlock::deserialize(file);
  if (!imageBlockUnique) {
    return nullptr;
  }

  std::shared_ptr<ImageBlock> imageBlock = std::move(imageBlockUnique);
  auto* pi = new (std::nothrow) PageImage(std::move(imageBlock), xPos, yPos);
  if (!pi) {
    LOG_ERR("PGE", "OOM: PageImage");
    cacheload::markOutOfMemory();
    return nullptr;
  }
  return std::unique_ptr<PageImage>(pi);
}

bool TableFragmentCell::serialize(serialization::BufferedWriter& file) const {
  if (lines.size() > MAX_SERIALIZED_LINES) {
    LOG_ERR("PTB", "Serialization failed: cell line count %u exceeds maximum", static_cast<uint32_t>(lines.size()));
    return false;
  }

  serialization::writePod(file, isHeader);
  serialization::writePod(file, static_cast<uint8_t>(lines.size()));
  for (const auto& line : lines) {
    if (!line || !line->serialize(file)) {
      LOG_ERR("PTB", "Serialization failed: invalid table cell line");
      return false;
    }
  }
  return true;
}

bool TableFragmentCell::deserialize(serialization::BufferedReader& file, TableFragmentCell& outCell) {
  uint8_t lineCount = 0;
  if (!serialization::readPod(file, outCell.isHeader) || !serialization::readPod(file, lineCount)) {
    LOG_ERR("PTB", "Deserialization failed: truncated table cell metadata");
    return false;
  }
  if (lineCount > MAX_SERIALIZED_LINES) {
    LOG_ERR("PTB", "Deserialization failed: cell line count %u exceeds maximum", lineCount);
    return false;
  }

  if (!canAllocateElements(lineCount, sizeof(std::shared_ptr<TextBlock>))) {
    LOG_ERR("PTB", "Deserialization failed: insufficient heap for table cell lines");
    cacheload::markOutOfMemory();
    return false;
  }
  outCell.lines.clear();
  outCell.lines.reserve(lineCount);
  for (uint8_t i = 0; i < lineCount; i++) {
    auto line = TextBlock::deserialize(file);
    if (!line) {
      LOG_ERR("PTB", "Deserialization failed: invalid table cell line");
      return false;
    }
    outCell.lines.push_back(std::move(line));
  }
  return true;
}

bool TableFragmentRow::serialize(serialization::BufferedWriter& file) const {
  if (cells.size() > MAX_SERIALIZED_CELLS) {
    LOG_ERR("PTB", "Serialization failed: row cell count %u exceeds maximum", static_cast<uint32_t>(cells.size()));
    return false;
  }

  serialization::writePod(file, height);
  serialization::writePod(file, headerSeparator);
  serialization::writePod(file, static_cast<uint8_t>(cells.size()));
  for (const auto& cell : cells) {
    if (!cell.serialize(file)) {
      return false;
    }
  }
  return true;
}

bool TableFragmentRow::deserialize(serialization::BufferedReader& file, TableFragmentRow& outRow) {
  uint8_t cellCount = 0;
  if (!serialization::readPod(file, outRow.height) || !serialization::readPod(file, outRow.headerSeparator) ||
      !serialization::readPod(file, cellCount)) {
    LOG_ERR("PTB", "Deserialization failed: truncated table row metadata");
    return false;
  }
  if (cellCount > MAX_SERIALIZED_CELLS) {
    LOG_ERR("PTB", "Deserialization failed: row cell count %u exceeds maximum", cellCount);
    return false;
  }

  if (!canAllocateElements(cellCount, sizeof(TableFragmentCell))) {
    LOG_ERR("PTB", "Deserialization failed: insufficient heap for table cells");
    cacheload::markOutOfMemory();
    return false;
  }
  outRow.cells.clear();
  outRow.cells.reserve(cellCount);
  for (uint8_t i = 0; i < cellCount; i++) {
    TableFragmentCell cell;
    if (!TableFragmentCell::deserialize(file, cell)) {
      return false;
    }
    outRow.cells.push_back(std::move(cell));
  }
  return true;
}

uint16_t PageTableFragment::getHeight() const {
  uint16_t total = 1;  // Bottom border.
  for (const auto& row : rows) {
    total = static_cast<uint16_t>(total + row.height);
  }
  return total;
}

void PageTableFragment::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  if (columnCount == 0 || rows.empty() || width < 2) {
    return;
  }

  const int drawX = xPos + xOffset;
  const int drawY = yPos + yOffset;
  const uint16_t totalHeight = getHeight();

  int16_t columnStarts[MAX_TABLE_CELLS_PER_ROW + 1] = {};
  for (uint8_t i = 0; i < columnCount; i++) {
    columnStarts[i] = static_cast<int16_t>((static_cast<uint32_t>(width) * i) / columnCount);
  }
  columnStarts[columnCount] = static_cast<int16_t>(width - 1);

  renderer.drawRect(drawX, drawY, width, totalHeight, true);
  for (uint8_t i = 1; i < columnCount; i++) {
    const int x = drawX + columnStarts[i];
    renderer.drawLine(x, drawY, x, drawY + totalHeight - 1, true);
  }

  int currentY = 0;
  for (size_t rowIndex = 0; rowIndex < rows.size(); rowIndex++) {
    const auto& row = rows[rowIndex];

    for (size_t colIndex = 0; colIndex < row.cells.size() && colIndex < columnCount; colIndex++) {
      const auto& cell = row.cells[colIndex];
      const int cellTextX = drawX + columnStarts[colIndex] + cellPadding;
      const int cellTextY = drawY + currentY + cellPadding;

      for (size_t lineIndex = 0; lineIndex < cell.lines.size(); lineIndex++) {
        cell.lines[lineIndex]->render(renderer, fontId, cellTextX,
                                      cellTextY + static_cast<int>(lineIndex) * lineHeight);
      }
    }

    currentY += row.height;
    if (rowIndex + 1 < rows.size()) {
      const int lineWidth = row.headerSeparator ? 2 : 1;
      renderer.drawLine(drawX, drawY + currentY, drawX + width - 1, drawY + currentY, lineWidth, true);
    }
  }
}

bool PageTableFragment::serialize(serialization::BufferedWriter& file) {
  if (rows.size() > MAX_SERIALIZED_ROWS) {
    LOG_ERR("PTB", "Serialization failed: fragment row count %u exceeds maximum", static_cast<uint32_t>(rows.size()));
    return false;
  }

  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, columnCount);
  serialization::writePod(file, cellPadding);
  serialization::writePod(file, lineHeight);
  serialization::writePod(file, static_cast<uint8_t>(rows.size()));
  for (const auto& row : rows) {
    if (!row.serialize(file)) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<PageTableFragment> PageTableFragment::deserialize(serialization::BufferedReader& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  uint8_t columnCount = 0;
  uint8_t cellPadding = 0;
  uint16_t lineHeight = 0;
  uint8_t rowCount = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos) ||
      !serialization::readPod(file, width) || !serialization::readPod(file, columnCount) ||
      !serialization::readPod(file, cellPadding) || !serialization::readPod(file, lineHeight) ||
      !serialization::readPod(file, rowCount)) {
    LOG_ERR("PTB", "Deserialization failed: truncated fragment metadata");
    return nullptr;
  }

  if (rowCount == 0 || rowCount > MAX_SERIALIZED_ROWS || columnCount == 0 ||
      columnCount > TableFragmentRow::MAX_SERIALIZED_CELLS || width < 2 || lineHeight == 0) {
    LOG_ERR("PTB", "Deserialization failed: invalid fragment metadata (rows=%u cols=%u width=%u lineHeight=%u)",
            rowCount, columnCount, width, lineHeight);
    return nullptr;
  }

  if (!canAllocateElements(rowCount, sizeof(TableFragmentRow))) {
    LOG_ERR("PTB", "Deserialization failed: insufficient heap for table rows");
    cacheload::markOutOfMemory();
    return nullptr;
  }
  std::vector<TableFragmentRow> rows;
  rows.reserve(rowCount);
  for (uint8_t i = 0; i < rowCount; i++) {
    TableFragmentRow row;
    if (!TableFragmentRow::deserialize(file, row)) {
      return nullptr;
    }
    rows.push_back(std::move(row));
  }

  auto* fragment =
      new (std::nothrow) PageTableFragment(width, columnCount, cellPadding, lineHeight, std::move(rows), xPos, yPos);
  if (!fragment) {
    LOG_ERR("PTB", "Deserialization failed: could not allocate PageTableFragment");
    cacheload::markOutOfMemory();
    return nullptr;
  }
  return std::unique_ptr<PageTableFragment>(fragment);
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset, [](const PageElement&) { return true; });
}

void Page::renderText(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return element.getTag() != TAG_PageImage; });
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return element.getTag() == TAG_PageImage; });
}

bool Page::serialize(serialization::BufferedWriter& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  serialization::writePod(file, fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  return true;
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }
  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
}

bool PageHorizontalRule::serialize(serialization::BufferedWriter& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, width);
  serialization::writePod(file, thickness);
  return true;
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(serialization::BufferedReader& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  uint8_t thickness = 0;
  if (!serialization::readPod(file, xPos) || !serialization::readPod(file, yPos) ||
      !serialization::readPod(file, width) || !serialization::readPod(file, thickness)) {
    LOG_ERR("PGE", "Deserialization failed: truncated PageHorizontalRule metadata");
    return nullptr;
  }

  if (width == 0 || thickness == 0) {
    LOG_ERR("PGE", "Deserialization failed: invalid horizontal rule metadata (width=%u thickness=%u)", width,
            thickness);
    return nullptr;
  }

  auto* rule = new (std::nothrow) PageHorizontalRule(width, thickness, xPos, yPos);
  if (!rule) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageHorizontalRule");
    cacheload::markOutOfMemory();
    return nullptr;
  }
  return std::unique_ptr<PageHorizontalRule>(rule);
}

std::unique_ptr<Page> Page::deserialize(serialization::BufferedReader& file) {
  auto page = std::unique_ptr<Page>(new (std::nothrow) Page());
  if (!page) {
    LOG_ERR("PGE", "OOM: Page");
    cacheload::markOutOfMemory();
    return nullptr;
  }

  uint16_t count = 0;
  if (!serialization::readPod(file, count) || count > MAX_PAGE_ELEMENTS) {
    LOG_ERR("PGE", "Deserialization failed: bad element count %u (max %u)", count, MAX_PAGE_ELEMENTS);
    return nullptr;
  }

  if (!canAllocateElements(count, sizeof(std::shared_ptr<PageElement>))) {
    LOG_ERR("PGE", "Deserialization failed: insufficient heap for page elements");
    cacheload::markOutOfMemory();
    return nullptr;
  }
  page->elements.reserve(count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag = 0;
    if (!serialization::readPod(file, tag)) {
      LOG_ERR("PGE", "Deserialization failed: truncated element tag");
      return nullptr;
    }

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        LOG_ERR("PGE", "Failed to deserialize PageImage");
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else if (tag == TAG_PageTableFragment) {
      auto fragment = PageTableFragment::deserialize(file);
      if (!fragment) {
        return nullptr;
      }
      page->elements.push_back(std::move(fragment));
    } else if (tag == TAG_PageHorizontalRule) {
      auto rule = PageHorizontalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount = 0;
  if (!serialization::readPod(file, fnCount)) {
    LOG_ERR("PGE", "Failed to read footnote count");
    return nullptr;
  }
  if (fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  if (!canAllocateElements(fnCount, sizeof(FootnoteEntry))) {
    LOG_ERR("PGE", "Deserialization failed: insufficient heap for footnotes");
    cacheload::markOutOfMemory();
    return nullptr;
  }
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  return page;
}
