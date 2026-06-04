#include "DayDetailActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SpiBusMutex.h"
#include "activities/todo/TodoInspectorSubactivity.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "core/features/FeatureCatalog.h"
#include "fontIds.h"
#include "util/DateUtils.h"

namespace {
constexpr int HEADER_HEIGHT = 70;
constexpr int ROW_HEIGHT = 38;
constexpr int GUTTER_WIDTH = 28;
constexpr int CHECKBOX_SIZE = 24;
constexpr int CHECKBOX_X = 12;
constexpr int PRIORITY_WIDTH = 18;
constexpr int TIME_WIDTH = 56;
constexpr int MARGIN_X = 10;
constexpr unsigned long LONG_CONFIRM_MS = 600;
constexpr unsigned long LONG_RIGHT_MS = 600;

bool isValidItemIndex(const int index, const size_t count) { return index >= 0 && static_cast<size_t>(index) < count; }

std::string trimEntryText(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
    start++;
  }
  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    end--;
  }
  std::string trimmed = text.substr(start, end - start);
  if (trimmed.size() > TodoPlannerStorage::kTodoEntryMaxTextLength) {
    trimmed.resize(TodoPlannerStorage::kTodoEntryMaxTextLength);
  }
  return trimmed;
}

TodoPriority cyclePriority(const TodoPriority priority, const int delta) {
  int value = static_cast<int>(priority) + delta;
  if (value < 0) {
    value = 3;
  } else if (value > 3) {
    value = 0;
  }
  return static_cast<TodoPriority>(value);
}

std::string formatDueLabel(const uint16_t dueMinutes) {
  if (dueMinutes == 0) {
    return {};
  }
  char buffer[8] = {};
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", dueMinutes / 60, dueMinutes % 60);
  return std::string(buffer);
}

std::string resolveDailyPath(const std::string& date) {
  const bool markdownEnabled = core::FeatureCatalog::isEnabled("markdown");
  const std::string markdownPath = "/daily/" + date + ".md";
  const std::string textPath = "/daily/" + date + ".txt";
  const bool markdownExists = Storage.exists(markdownPath.c_str());
  const bool textExists = Storage.exists(textPath.c_str());
  return TodoPlannerStorage::dailyPath(date, markdownEnabled, markdownExists, textExists);
}
}  // namespace

DayDetailActivity::DayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                                     std::string isoDate, std::string dateTitle, void* onBackCtx, void (*onBack)(void*))
    : ActivityWithSubactivity("DayDetail", renderer, mappedInput),
      filePath(std::move(filePath)),
      isoDate(std::move(isoDate)),
      dateTitle(std::move(dateTitle)),
      onBackCtx(onBackCtx),
      onBack(onBack) {}

void DayDetailActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  skipInitialInput = true;
  priorityAdjustLatched = false;
  loadTasks();
  selectedIndex = 0;
  scrollOffset = 0;
  requestUpdate();
}

void DayDetailActivity::onExit() { ActivityWithSubactivity::onExit(); }

void DayDetailActivity::clearPriorityLatch() {
  if (priorityAdjustLatched) {
    priorityAdjustLatched = false;
    requestUpdate();
  }
}

bool DayDetailActivity::isEmptyDay() const { return items.empty(); }

bool DayDetailActivity::isTaskRow(const int index) const {
  return isValidItemIndex(index, items.size()) && !items[static_cast<size_t>(index)].isHeader;
}

int DayDetailActivity::countDoneTasks() const {
  return static_cast<int>(
      std::count_if(items.begin(), items.end(), [](const TodoItem& item) { return !item.isHeader && item.checked; }));
}

int DayDetailActivity::countTotalTasks() const {
  return static_cast<int>(
      std::count_if(items.begin(), items.end(), [](const TodoItem& item) { return !item.isHeader; }));
}

bool DayDetailActivity::hasPrevDay() const {
  if (isoDate.empty()) {
    return false;
  }
  const std::string prevDate = DateUtils::offsetDate(isoDate, -1);
  return !prevDate.empty() && DateUtils::dailyFileExists(prevDate, core::FeatureCatalog::isEnabled("markdown"));
}

bool DayDetailActivity::hasNextDay() const {
  if (isoDate.empty()) {
    return false;
  }
  const std::string nextDate = DateUtils::offsetDate(isoDate, 1);
  return !nextDate.empty() && DateUtils::dailyFileExists(nextDate, core::FeatureCatalog::isEnabled("markdown"));
}

void DayDetailActivity::switchToDate(const std::string& newDate) {
  if (newDate.empty()) {
    return;
  }
  isoDate = newDate;
  filePath = resolveDailyPath(newDate);
  dateTitle = DateUtils::formatDayTitle(newDate);
  selectedIndex = 0;
  scrollOffset = 0;
  clearPriorityLatch();
  loadTasks();
  requestUpdate();
}

void DayDetailActivity::navigateDay(const int delta) {
  if (isoDate.empty()) {
    return;
  }
  const std::string targetDate = DateUtils::offsetDate(isoDate, delta);
  if (targetDate.empty()) {
    return;
  }
  if (!DateUtils::dailyFileExists(targetDate, core::FeatureCatalog::isEnabled("markdown"))) {
    return;
  }
  switchToDate(targetDate);
}

void DayDetailActivity::loadTasks() {
  SpiBusMutex::Guard guard;
  items.clear();
  if (!Storage.exists(filePath.c_str())) {
    return;
  }
  TodoPlannerStorage::parseFile(Storage.readFile(filePath.c_str()).c_str(), items);
}

void DayDetailActivity::saveTasks() {
  SpiBusMutex::Guard guard;

  const auto slashPos = filePath.find_last_of('/');
  if (slashPos != std::string::npos && slashPos > 0) {
    Storage.mkdir(filePath.substr(0, slashPos).c_str());
  }

  const std::string tempPath = filePath + ".tmp";
  const std::string backupPath = filePath + ".bak";
  const bool markdownFile = filePath.size() >= 3 && filePath.compare(filePath.size() - 3, 3, ".md") == 0;
  const std::string content = TodoPlannerStorage::formatFile(items, markdownFile);

  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite("TDO", tempPath.c_str(), file)) {
    return;
  }
  if (file.print(content.c_str()) == 0) {
    file.close();
    Storage.remove(tempPath.c_str());
    return;
  }
  file.close();

  const bool hasExisting = Storage.exists(filePath.c_str());
  if (Storage.exists(backupPath.c_str())) {
    Storage.remove(backupPath.c_str());
  }
  if (hasExisting && !Storage.rename(filePath.c_str(), backupPath.c_str())) {
    Storage.remove(tempPath.c_str());
    return;
  }
  if (!Storage.rename(tempPath.c_str(), filePath.c_str())) {
    if (hasExisting) {
      Storage.rename(backupPath.c_str(), filePath.c_str());
    }
    Storage.remove(tempPath.c_str());
    return;
  }
  if (hasExisting) {
    Storage.remove(backupPath.c_str());
  }
}

void DayDetailActivity::toggleCurrentTask() {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  auto& item = items[static_cast<size_t>(selectedIndex)];
  item.checked = !item.checked;
  saveTasks();
  requestUpdate();
}

void DayDetailActivity::addNewEntry(const bool sectionEntry) {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                             renderer, mappedInput, sectionEntry ? tr(STR_TODO_NEW_SECTION) : tr(STR_TODO_NEW_TASK), "",
                             TodoPlannerStorage::kTodoEntryMaxTextLength, false, 10),
                         [this, sectionEntry](const ActivityResult& result) {
                           if (result.isCancelled) {
                             return;
                           }
                           const std::string trimmedText = trimEntryText(std::get<KeyboardResult>(result.data).text);
                           if (trimmedText.empty()) {
                             return;
                           }
                           TodoItem newItem;
                           newItem.text = trimmedText;
                           newItem.isHeader = sectionEntry;
                           newItem.isSection = sectionEntry;
                           items.push_back(newItem);
                           saveTasks();
                           selectedIndex = static_cast<int>(items.size()) - 1;
                           requestUpdate();
                         });
}

void DayDetailActivity::editCurrentEntry() {
  if (!isValidItemIndex(selectedIndex, items.size())) {
    return;
  }
  const size_t editIndex = static_cast<size_t>(selectedIndex);
  const TodoItem& current = items[editIndex];
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                              current.isHeader ? tr(STR_TODO_EDIT_SECTION) : tr(STR_TODO_EDIT_TASK),
                                              current.text, TodoPlannerStorage::kTodoEntryMaxTextLength, false, 10),
      [this, editIndex](const ActivityResult& result) {
        if (result.isCancelled || !isValidItemIndex(static_cast<int>(editIndex), items.size())) {
          return;
        }
        const std::string trimmedText = trimEntryText(std::get<KeyboardResult>(result.data).text);
        if (trimmedText.empty()) {
          items.erase(items.begin() + static_cast<std::vector<TodoItem>::difference_type>(editIndex));
          selectedIndex = std::min(selectedIndex, static_cast<int>(items.size()) - 1);
        } else {
          items[editIndex].text = trimmedText;
        }
        saveTasks();
        requestUpdate();
      });
}

void DayDetailActivity::openInspector() {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  // enterNewActivity null-checks its argument; nothrow OOM degrades to a no-op.
  enterNewActivity(new (std::nothrow) TodoInspectorSubactivity(renderer, mappedInput, *this, selectedIndex));
}

void DayDetailActivity::moveSelectedTask(const int delta) {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  int targetIndex = selectedIndex + delta;
  while (targetIndex >= 0 && targetIndex < static_cast<int>(items.size()) &&
         items[static_cast<size_t>(targetIndex)].isHeader) {
    targetIndex += delta;
  }
  if (targetIndex < 0 || targetIndex >= static_cast<int>(items.size()) ||
      items[static_cast<size_t>(targetIndex)].isHeader) {
    return;
  }
  std::swap(items[static_cast<size_t>(selectedIndex)], items[static_cast<size_t>(targetIndex)]);
  selectedIndex = targetIndex;
  saveTasks();
  requestUpdate();
}

void DayDetailActivity::deleteSelectedTask() {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  items.erase(items.begin() + static_cast<std::vector<TodoItem>::difference_type>(selectedIndex));
  if (selectedIndex >= static_cast<int>(items.size())) {
    selectedIndex = static_cast<int>(items.size()) - 1;
  }
  saveTasks();
  requestUpdate();
}

void DayDetailActivity::cycleSelectedPriority(const int delta) {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  auto& item = items[static_cast<size_t>(selectedIndex)];
  item.priority = cyclePriority(item.priority, delta);
  saveTasks();
  requestUpdate();
}

void DayDetailActivity::setSelectedDueMinutes(const uint16_t dueMinutes) {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  items[static_cast<size_t>(selectedIndex)].dueMinutes = dueMinutes;
  saveTasks();
  requestUpdate();
}

void DayDetailActivity::editSelectedTaskText() {
  closeInspector();
  editCurrentEntry();
}

void DayDetailActivity::editSelectedDueTime() {
  if (!isTaskRow(selectedIndex)) {
    return;
  }
  const TodoItem& item = items[static_cast<size_t>(selectedIndex)];
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(
          renderer, mappedInput, tr(STR_TODO_INSPECTOR_SET_TIME),
          formatDueLabel(item.dueMinutes).empty() ? "09:00" : formatDueLabel(item.dueMinutes), 5, false, 5),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }
        const std::string text = std::get<KeyboardResult>(result.data).text;
        if (text.size() == 5 && text[2] == ':') {
          const int hour = (text[0] - '0') * 10 + (text[1] - '0');
          const int minute = (text[3] - '0') * 10 + (text[4] - '0');
          if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            setSelectedDueMinutes(static_cast<uint16_t>(hour * 60 + minute));
          }
        }
      });
}

void DayDetailActivity::closeInspector() { exitActivity(); }

void DayDetailActivity::focusTaskIndex(const int index) {
  if (isValidItemIndex(index, items.size())) {
    selectedIndex = index;
  }
}

void DayDetailActivity::loop() {
  ActivityWithSubactivity::loop();
  if (subActivity) {
    return;
  }

  if (skipInitialInput) {
    const bool clear = !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                       !mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
                       !mappedInput.isPressed(MappedInputManager::Button::Back) &&
                       !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (clear) {
      skipInitialInput = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack != nullptr) {
      onBack(onBackCtx);
    }
    return;
  }

  const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool rightReleased = mappedInput.wasReleased(MappedInputManager::Button::Right);
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool longConfirm = confirmReleased && mappedInput.getHeldTime() >= LONG_CONFIRM_MS;
  const bool longRight = rightReleased && mappedInput.getHeldTime() >= LONG_RIGHT_MS;
  const int visibleRows = (renderer.getScreenHeight() - HEADER_HEIGHT) / ROW_HEIGHT;
  const int totalRows = isEmptyDay() ? 0 : static_cast<int>(items.size());

  if (upPressed || downPressed || confirmReleased) {
    clearPriorityLatch();
  }

  if (upPressed && totalRows > 0 && selectedIndex > 0) {
    selectedIndex--;
    if (selectedIndex < scrollOffset) {
      scrollOffset = selectedIndex;
    }
    requestUpdate();
  } else if (downPressed && totalRows > 0 && selectedIndex < totalRows - 1) {
    selectedIndex++;
    if (selectedIndex >= scrollOffset + visibleRows) {
      scrollOffset = selectedIndex - visibleRows + 1;
    }
    requestUpdate();
  }

  const bool sectionOrEmpty = isEmptyDay() || (isValidItemIndex(selectedIndex, items.size()) &&
                                               items[static_cast<size_t>(selectedIndex)].isHeader);
  if (sectionOrEmpty) {
    if (leftPressed && hasPrevDay()) {
      navigateDay(-1);
    } else if (rightPressed && hasNextDay()) {
      navigateDay(1);
    }
  } else if (isTaskRow(selectedIndex)) {
    if (longRight && hasNextDay()) {
      navigateDay(1);
    } else if (priorityAdjustLatched) {
      if (leftPressed) {
        cycleSelectedPriority(-1);
      } else if (rightPressed) {
        cycleSelectedPriority(1);
      }
    } else {
      if (leftPressed && hasPrevDay()) {
        navigateDay(-1);
      } else if (rightPressed) {
        priorityAdjustLatched = true;
        requestUpdate();
      }
    }
  }

  if (confirmReleased) {
    if (isEmptyDay()) {
      addNewEntry(false);
    } else if (isValidItemIndex(selectedIndex, items.size())) {
      const TodoItem& item = items[static_cast<size_t>(selectedIndex)];
      if (item.isHeader) {
        if (longConfirm) {
          editCurrentEntry();
        }
      } else if (longConfirm) {
        openInspector();
      } else {
        toggleCurrentTask();
      }
    }
  } else if (longConfirm) {
    if (isEmptyDay()) {
      addNewEntry(true);
    }
  }
}

void DayDetailActivity::render(Activity::RenderLock&& lock) { renderScreen(); }

void DayDetailActivity::renderHeader() const {
  if (hasPrevDay()) {
    renderer.drawText(UI_10_FONT_ID, MARGIN_X, 24, "<", true);
  }
  if (hasNextDay()) {
    renderer.drawText(UI_10_FONT_ID, renderer.getScreenWidth() - MARGIN_X - 8, 24, ">", true);
  }

  renderer.drawCenteredText(UI_12_FONT_ID, 18, dateTitle.c_str(), true, EpdFontFamily::BOLD);
  char progress[32] = {};
  std::snprintf(progress, sizeof(progress), tr(STR_TODO_DONE_OF_TOTAL), countDoneTasks(), countTotalTasks());
  renderer.drawCenteredText(UI_10_FONT_ID, 42, progress, true);
  renderer.drawLine(0, HEADER_HEIGHT - 1, renderer.getScreenWidth(), HEADER_HEIGHT - 1);
}

void DayDetailActivity::renderEmptyState() const {
  renderer.drawCenteredText(UI_12_FONT_ID, HEADER_HEIGHT + 80, tr(STR_TODO_FRESH_PAGE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, HEADER_HEIGHT + 120, tr(STR_TODO_FRESH_PAGE_HINT));
}

void DayDetailActivity::renderRow(const int y, const int itemIndex, const bool selected) const {
  if (!isValidItemIndex(itemIndex, items.size())) {
    return;
  }

  const TodoItem& item = items[static_cast<size_t>(itemIndex)];
  if (selected) {
    renderer.fillRect(0, y, GUTTER_WIDTH, ROW_HEIGHT);
  }
  renderer.drawLine(0, y + ROW_HEIGHT - 1, renderer.getScreenWidth(), y + ROW_HEIGHT - 1);

  if (item.isHeader) {
    const int textY = y + (ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, MARGIN_X + GUTTER_WIDTH, textY, item.text.c_str(), !selected);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, item.text.c_str());
    renderer.drawLine(MARGIN_X + GUTTER_WIDTH, y + ROW_HEIGHT - 4, MARGIN_X + GUTTER_WIDTH + textWidth,
                      y + ROW_HEIGHT - 4, !selected);
    return;
  }

  const int boxY = y + (ROW_HEIGHT - CHECKBOX_SIZE) / 2;
  renderer.drawRect(CHECKBOX_X, boxY + 1, CHECKBOX_SIZE, CHECKBOX_SIZE, !selected);
  renderer.drawRect(CHECKBOX_X + 1, boxY, CHECKBOX_SIZE, CHECKBOX_SIZE, !selected);
  if (item.checked) {
    renderer.drawLine(CHECKBOX_X + 5, boxY + 10, CHECKBOX_X + 10, boxY + 15, !selected);
    renderer.drawLine(CHECKBOX_X + 10, boxY + 15, CHECKBOX_X + 19, boxY + 6, !selected);
  }

  int textX = CHECKBOX_X + CHECKBOX_SIZE + PRIORITY_WIDTH + 8;
  if (item.priority != TodoPriority::None) {
    const bool filled = item.priority == TodoPriority::P1;
    renderer.drawText(UI_10_FONT_ID, CHECKBOX_X + CHECKBOX_SIZE + 4, y + 8, filled ? "▲" : "△", !selected);
  }

  const std::string dueLabel = formatDueLabel(item.dueMinutes);
  const int timeX = renderer.getScreenWidth() - TIME_WIDTH;
  if (!dueLabel.empty()) {
    renderer.drawText(UI_10_FONT_ID, timeX, y + 10, dueLabel.c_str(), !selected);
  }

  const int maxTextWidth = timeX - textX - MARGIN_X;
  const std::string text = renderer.truncatedText(UI_10_FONT_ID, item.text.c_str(), maxTextWidth);
  const int textY = y + (ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, textX, textY, text.c_str(), !selected);
  if (item.checked) {
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, text.c_str());
    renderer.drawLine(textX, textY + renderer.getLineHeight(UI_10_FONT_ID) / 2, textX + textWidth,
                      textY + renderer.getLineHeight(UI_10_FONT_ID) / 2, !selected);
  }
}

void DayDetailActivity::renderFooterHints() const {
  const char* leftLabel = "";
  const char* rightLabel = "";
  if (priorityAdjustLatched && isTaskRow(selectedIndex)) {
    leftLabel = tr(STR_TODO_FOOTER_LOWER);
    rightLabel = tr(STR_TODO_FOOTER_HIGHER);
  } else {
    leftLabel = tr(STR_TODO_FOOTER_PREV_DAY);
    rightLabel = tr(STR_TODO_FOOTER_NEXT_DAY);
  }

  const char* confirmLabel = isEmptyDay() ? tr(STR_TODO_NEW_TASK) : (isTaskRow(selectedIndex) ? "Toggle" : "Edit");
  const auto labels = mappedInput.mapLabels("Back", confirmLabel, leftLabel, rightLabel);
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DayDetailActivity::renderScreen() {
  renderer.clearScreen();
  renderHeader();

  if (isEmptyDay()) {
    renderEmptyState();
  } else {
    const int visibleRows = (renderer.getScreenHeight() - HEADER_HEIGHT) / ROW_HEIGHT;
    for (int row = 0; row < visibleRows; ++row) {
      const int itemIndex = scrollOffset + row;
      if (itemIndex >= static_cast<int>(items.size())) {
        break;
      }
      renderRow(HEADER_HEIGHT + row * ROW_HEIGHT, itemIndex, itemIndex == selectedIndex);
    }
  }

  renderFooterHints();
  renderer.displayBuffer();
}
