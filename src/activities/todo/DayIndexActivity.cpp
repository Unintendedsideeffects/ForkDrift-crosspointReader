#include "DayIndexActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "MappedInputManager.h"
#include "SpiBusMutex.h"
#include "activities/ActivityManager.h"
#include "activities/todo/DayDetailActivity.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "core/features/FeatureCatalog.h"
#include "fontIds.h"
#include "network/server/FileListApi.h"
#include "util/DateUtils.h"

namespace {
constexpr int HEADER_HEIGHT = 50;
constexpr int ROW_HEIGHT = 38;
constexpr int GUTTER_WIDTH = 8;
constexpr int MARGIN_X = 14;
constexpr unsigned long LONG_CONFIRM_MS = 600;

bool isIsoDailyBasename(const std::string& basename, std::string& isoDateOut) {
  const bool isMarkdown = basename.size() == 13 && basename.compare(10, 3, ".md") == 0;
  const bool isText = basename.size() == 14 && basename.compare(10, 4, ".txt") == 0;
  if (!isMarkdown && !isText) {
    return false;
  }

  const std::string stem = basename.substr(0, 10);
  if (stem[4] != '-' || stem[7] != '-') {
    return false;
  }
  for (size_t i = 0; i < stem.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
      return false;
    }
  }

  isoDateOut = stem;
  return true;
}

void countTodoLines(const std::string& content, int& openCount, int& doneCount) {
  openCount = 0;
  doneCount = 0;
  std::string line;
  for (const char c : content) {
    if (c == '\n') {
      if (line.rfind("- [ ]", 0) == 0) {
        openCount++;
      } else if (line.rfind("- [x]", 0) == 0 || line.rfind("- [X]", 0) == 0) {
        doneCount++;
      }
      line.clear();
    } else if (c != '\r') {
      line.push_back(c);
    }
  }
  if (!line.empty()) {
    if (line.rfind("- [ ]", 0) == 0) {
      openCount++;
    } else if (line.rfind("- [x]", 0) == 0 || line.rfind("- [X]", 0) == 0) {
      doneCount++;
    }
  }
}

void returnToDayIndex(void* ctx) {
  auto& manager = *static_cast<ActivityManager*>(ctx);
  manager.replaceActivity(
      std::make_unique<DayIndexActivity>(manager.getRenderer(), manager.getMappedInput(), &manager,
                                         [](void* backCtx) { static_cast<ActivityManager*>(backCtx)->goHome(); }));
}
}  // namespace

DayIndexActivity::DayIndexActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void* onBackCtx,
                                   void (*onBack)(void*))
    : Activity("DayIndex", renderer, mappedInput), onBackCtx(onBackCtx), onBack(onBack) {}

void DayIndexActivity::onEnter() {
  Activity::onEnter();
  skipInitialInput = true;
  today = DateUtils::currentDate();
  loadEntries();
  selectedIndex = 0;
  scrollOffset = 0;
  if (!today.empty()) {
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].isoDate == today) {
        selectedIndex = static_cast<int>(i);
        break;
      }
    }
  }
  requestUpdate();
}

void DayIndexActivity::onExit() { Activity::onExit(); }

void DayIndexActivity::loadEntries() {
  SpiBusMutex::Guard guard;
  entries.clear();

  network::scanDirectory("/daily", false, [&](const network::DirEntry& entry) {
    if (entry.isDirectory) {
      return;
    }
    std::string isoDate;
    if (!isIsoDailyBasename(entry.name.c_str(), isoDate)) {
      return;
    }

    DayIndexEntry dayEntry;
    dayEntry.isoDate = isoDate;
    const std::string path = TodoPlannerStorage::resolveDailyPath(isoDate, core::FeatureCatalog::isEnabled("markdown"));
    if (Storage.exists(path.c_str())) {
      countTodoLines(TodoPlannerStorage::readDailyFileCapped(path), dayEntry.openCount, dayEntry.doneCount);
    }
    entries.push_back(std::move(dayEntry));
  });

  std::sort(entries.begin(), entries.end(),
            [](const DayIndexEntry& a, const DayIndexEntry& b) { return a.isoDate > b.isoDate; });
}

void DayIndexActivity::openSelectedDay() {
  if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= entries.size()) {
    return;
  }

  const std::string& isoDate = entries[static_cast<size_t>(selectedIndex)].isoDate;
  const bool markdownEnabled = core::FeatureCatalog::isEnabled("markdown");
  const std::string filePath = TodoPlannerStorage::resolveDailyPath(isoDate, markdownEnabled);
  const std::string dateTitle = DateUtils::formatDayTitle(isoDate);

  activityManager.replaceActivity(std::make_unique<DayDetailActivity>(renderer, mappedInput, filePath, isoDate,
                                                                      dateTitle, &activityManager, returnToDayIndex));
}

void DayIndexActivity::loop() {
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
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool longConfirm = confirmReleased && mappedInput.getHeldTime() >= LONG_CONFIRM_MS;
  const int visibleRows = (renderer.getScreenHeight() - HEADER_HEIGHT) / ROW_HEIGHT;

  if (longConfirm && !today.empty()) {
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].isoDate == today) {
        selectedIndex = static_cast<int>(i);
        if (selectedIndex < scrollOffset) {
          scrollOffset = selectedIndex;
        } else if (selectedIndex >= scrollOffset + visibleRows) {
          scrollOffset = selectedIndex - visibleRows + 1;
        }
        requestUpdate();
        return;
      }
    }
  }

  if (upPressed && selectedIndex > 0) {
    selectedIndex--;
    if (selectedIndex < scrollOffset) {
      scrollOffset = selectedIndex;
    }
    requestUpdate();
  } else if (downPressed && selectedIndex < static_cast<int>(entries.size()) - 1) {
    selectedIndex++;
    if (selectedIndex >= scrollOffset + visibleRows) {
      scrollOffset = selectedIndex - visibleRows + 1;
    }
    requestUpdate();
  }

  if (confirmReleased && !entries.empty()) {
    openSelectedDay();
  }
}

void DayIndexActivity::render(Activity::RenderLock&& lock) { renderScreen(); }

void DayIndexActivity::renderScreen() {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_TODO_DAY_INDEX_TITLE), true, EpdFontFamily::BOLD);
  renderer.drawLine(0, HEADER_HEIGHT, renderer.getScreenWidth(), HEADER_HEIGHT);

  const int visibleRows = (renderer.getScreenHeight() - HEADER_HEIGHT) / ROW_HEIGHT;
  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, HEADER_HEIGHT + 40, tr(STR_TODO_FRESH_PAGE));
  } else {
    for (int row = 0; row < visibleRows; ++row) {
      const int entryIndex = scrollOffset + row;
      if (entryIndex >= static_cast<int>(entries.size())) {
        break;
      }

      const int y = HEADER_HEIGHT + row * ROW_HEIGHT;
      const bool selected = entryIndex == selectedIndex;
      const DayIndexEntry& entry = entries[static_cast<size_t>(entryIndex)];

      if (selected) {
        renderer.fillRect(0, y, GUTTER_WIDTH, ROW_HEIGHT);
      }

      renderer.drawLine(0, y + ROW_HEIGHT - 1, renderer.getScreenWidth(), y + ROW_HEIGHT - 1);

      const bool isToday = !today.empty() && entry.isoDate == today;
      char countBuffer[16] = {};
      std::snprintf(countBuffer, sizeof(countBuffer), "%d/%d", entry.openCount, entry.openCount + entry.doneCount);
      const std::string label = DateUtils::formatDayIndexLabel(entry.isoDate) + " — " + countBuffer;
      const int textY = y + (ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, MARGIN_X, textY, label.c_str(), !selected || isToday);
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "Open", "", "Today");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
