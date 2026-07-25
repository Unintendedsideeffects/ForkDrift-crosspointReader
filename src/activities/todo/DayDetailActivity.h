#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"
#include "TodoItem.h"

class DayDetailActivity final : public ActivityWithSubactivity {
 public:
  DayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath, std::string isoDate,
                    std::string dateTitle, void* onBackCtx, void (*onBack)(void*));

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;

  void moveSelectedTask(int delta);
  void deleteSelectedTask();
  void cycleSelectedPriority(int delta);
  void setSelectedDueMinutes(uint16_t dueMinutes);
  void editSelectedTaskText();
  void editSelectedDueTime();
  void cycleSelectedRecurrence();
  void closeInspector();
  void addNewEntry(bool sectionEntry);
  void focusTaskIndex(int index);

 private:
  std::string filePath;
  std::string isoDate;
  std::string dateTitle;
  void* onBackCtx;
  void (*onBack)(void*);

  std::vector<TodoItem> items;
  int selectedIndex = 0;
  int scrollOffset = 0;
  bool skipInitialInput = true;
  bool priorityAdjustLatched = false;

  void loadTasks();
  void saveTasks();
  void toggleCurrentTask();
  void editCurrentEntry();
  void openInspector();
  void navigateDay(int delta);
  void switchToDate(const std::string& newDate);
  void clearPriorityLatch();

  int countDoneTasks() const;
  int countTotalTasks() const;
  bool hasPrevDay() const;
  bool hasNextDay() const;
  bool isTaskRow(int index) const;
  bool isEmptyDay() const;
  // The list always ends with a synthetic "new task" row, so adding an entry
  // never depends on the day being empty or on a header being selected.
  int rowCount() const;
  bool isAddRow(int index) const;

  void renderScreen();
  void renderHeader() const;
  void renderRow(int y, int itemIndex, bool selected) const;
  void renderAddRow(int y, bool selected) const;
  void renderEmptyState() const;
  void renderFooterHints() const;
};
