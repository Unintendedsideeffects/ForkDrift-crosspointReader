#pragma once

#include <cstdint>
#include <string>

enum class TodoPriority : uint8_t { None, P1, P2, P3 };
enum class TodoRecurrence : uint8_t { None, Daily, Weekly, Weekdays };

struct TodoItem {
  std::string text;
  bool checked = false;
  bool isHeader = false;
  bool isAgenda = false;
  bool isSection = false;
  TodoPriority priority = TodoPriority::None;
  uint16_t dueMinutes = 0;
  TodoRecurrence recurrence = TodoRecurrence::None;
  uint8_t weekdayMask = 0;  // bit 0 = Mon .. bit 6 = Sun; used only when Weekdays
};
