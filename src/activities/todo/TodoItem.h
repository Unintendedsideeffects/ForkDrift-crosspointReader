#pragma once

#include <cstdint>
#include <string>

enum class TodoPriority : uint8_t { None, P1, P2, P3 };

struct TodoItem {
  std::string text;
  bool checked = false;
  bool isHeader = false;
  bool isAgenda = false;
  bool isSection = false;
  TodoPriority priority = TodoPriority::None;
  uint16_t dueMinutes = 0;
};
