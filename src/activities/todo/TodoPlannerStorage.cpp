#include "activities/todo/TodoPlannerStorage.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace TodoPlannerStorage {
namespace {

bool startsWith(const std::string& line, const char* prefix) {
  const size_t len = std::strlen(prefix);
  return line.size() >= len && line.compare(0, len, prefix) == 0;
}

bool parsePriorityToken(const std::string& token, TodoPriority& priority) {
  if (token == "!p1") {
    priority = TodoPriority::P1;
    return true;
  }
  if (token == "!p2") {
    priority = TodoPriority::P2;
    return true;
  }
  if (token == "!p3") {
    priority = TodoPriority::P3;
    return true;
  }
  return false;
}

bool parseDueTimePrefix(const std::string& remainder, uint16_t& dueMinutes, size_t& consumed) {
  if (remainder.size() < 6 || remainder[2] != ':' || remainder[5] != ' ') {
    return false;
  }
  if (!std::isdigit(static_cast<unsigned char>(remainder[0])) ||
      !std::isdigit(static_cast<unsigned char>(remainder[1])) ||
      !std::isdigit(static_cast<unsigned char>(remainder[3])) ||
      !std::isdigit(static_cast<unsigned char>(remainder[4]))) {
    return false;
  }

  const int hour = (remainder[0] - '0') * 10 + (remainder[1] - '0');
  const int minute = (remainder[3] - '0') * 10 + (remainder[4] - '0');
  if (hour > 23 || minute > 59) {
    return false;
  }

  dueMinutes = static_cast<uint16_t>(hour * 60 + minute);
  consumed = 6;
  return true;
}

void peelTodoPrefixes(std::string& remainder, TodoItem& item) {
  while (!remainder.empty()) {
    if (remainder[0] == '!') {
      const size_t spacePos = remainder.find(' ');
      const std::string token = spacePos == std::string::npos ? remainder : remainder.substr(0, spacePos);
      TodoPriority priority = TodoPriority::None;
      if (!parsePriorityToken(token, priority)) {
        break;
      }
      item.priority = priority;
      remainder = spacePos == std::string::npos ? std::string{} : remainder.substr(spacePos + 1);
      continue;
    }

    size_t consumed = 0;
    uint16_t dueMinutes = 0;
    if (parseDueTimePrefix(remainder, dueMinutes, consumed)) {
      item.dueMinutes = dueMinutes;
      remainder.erase(0, consumed);
      continue;
    }
    break;
  }
  item.text = remainder;
}

const char* priorityToken(const TodoPriority priority) {
  switch (priority) {
    case TodoPriority::P1:
      return "!p1";
    case TodoPriority::P2:
      return "!p2";
    case TodoPriority::P3:
      return "!p3";
    case TodoPriority::None:
      break;
  }
  return nullptr;
}

void appendDueTime(std::string& line, const uint16_t dueMinutes) {
  if (dueMinutes == 0) {
    return;
  }
  const int hour = dueMinutes / 60;
  const int minute = dueMinutes % 60;
  char buffer[7] = {};
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d ", hour, minute);
  line += buffer;
}

}  // namespace

std::string dailyPath(const std::string& date, const bool markdownEnabled, const bool markdownExists,
                      const bool textExists) {
  if (markdownExists) {
    return "/daily/" + date + ".md";
  }
  if (textExists) {
    return "/daily/" + date + ".txt";
  }
  return "/daily/" + date + (markdownEnabled ? ".md" : ".txt");
}

std::string formatEntry(const std::string& text, const bool agendaEntry, const bool markdownEnabled) {
  if (agendaEntry) {
    return markdownEnabled ? "> " + text : text;
  }
  return "- [ ] " + text;
}

bool parseLine(std::string line, TodoItem& out) {
  out = TodoItem{};
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  if (line.empty()) {
    return false;
  }

  if (startsWith(line, "- [ ] ")) {
    out.checked = false;
    out.isHeader = false;
    std::string remainder = line.substr(6);
    peelTodoPrefixes(remainder, out);
    return true;
  }
  if (startsWith(line, "- [x] ") || startsWith(line, "- [X] ")) {
    out.checked = true;
    out.isHeader = false;
    std::string remainder = line.substr(6);
    peelTodoPrefixes(remainder, out);
    return true;
  }
  if (startsWith(line, "> ")) {
    out.isHeader = true;
    out.isAgenda = true;
    out.text = line.substr(2);
    return true;
  }
  if (startsWith(line, "## ")) {
    out.isHeader = true;
    out.isSection = true;
    out.text = line.substr(3);
    return true;
  }

  out.isHeader = true;
  out.text = line;
  return true;
}

std::string formatItem(const TodoItem& item, const bool markdownFile) {
  if (item.isHeader) {
    std::string line;
    if (item.isSection) {
      line = "## " + item.text;
    } else if (item.isAgenda && markdownFile) {
      line = "> " + item.text;
    } else {
      line = item.text;
    }
    return line;
  }

  std::string line = "- [";
  line += item.checked ? 'x' : ' ';
  line += "] ";
  if (const char* token = priorityToken(item.priority); token != nullptr) {
    line += token;
    line += ' ';
  }
  appendDueTime(line, item.dueMinutes);
  line += item.text;
  return line;
}

void parseFile(const std::string& content, std::vector<TodoItem>& out) {
  out.clear();
  std::string line;
  line.reserve(128);
  for (const char c : content) {
    if (c == '\n') {
      TodoItem item;
      if (parseLine(line, item)) {
        out.push_back(std::move(item));
      }
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  if (!line.empty()) {
    TodoItem item;
    if (parseLine(line, item)) {
      out.push_back(std::move(item));
    }
  }
}

std::string formatFile(const std::vector<TodoItem>& items, const bool markdownFile) {
  std::string content;
  for (const TodoItem& item : items) {
    content += formatItem(item, markdownFile);
    content.push_back('\n');
  }
  return content;
}

}  // namespace TodoPlannerStorage
