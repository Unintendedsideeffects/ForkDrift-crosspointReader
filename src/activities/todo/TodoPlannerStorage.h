#pragma once

#include <string>
#include <vector>

#include "activities/todo/TodoItem.h"

namespace TodoPlannerStorage {

constexpr size_t kTodoEntryMaxTextLength = 300;

std::string dailyPath(const std::string& date, bool markdownEnabled, bool markdownExists, bool textExists);

std::string formatEntry(const std::string& text, bool agendaEntry, bool markdownEnabled = false);

bool parseLine(std::string line, TodoItem& out);

std::string formatItem(const TodoItem& item, bool markdownFile);

void parseFile(const std::string& content, std::vector<TodoItem>& out);

std::string formatFile(const std::vector<TodoItem>& items, bool markdownFile);

}  // namespace TodoPlannerStorage
