#include "activities/todo/TodoPlannerStorage.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "util/DateUtils.h"

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
      if (parsePriorityToken(token, priority)) {
        item.priority = priority;
        remainder = spacePos == std::string::npos ? std::string{} : remainder.substr(spacePos + 1);
        continue;
      }
      TodoRecurrence recurrence = TodoRecurrence::None;
      uint8_t mask = 0;
      if (parseRecurrenceToken(token, recurrence, mask)) {
        item.recurrence = recurrence;
        item.weekdayMask = mask;
        remainder = spacePos == std::string::npos ? std::string{} : remainder.substr(spacePos + 1);
        continue;
      }
      break;
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

std::string readDailyFileCapped(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    const std::string tmpPath = path + ".tmp";
    const std::string bakPath = path + ".bak";
    if (Storage.exists(tmpPath.c_str())) {
      if (Storage.rename(tmpPath.c_str(), path.c_str())) {
        LOG_INF("TODO", "Recovered daily file from interrupted save");
      }
    } else if (Storage.exists(bakPath.c_str())) {
      if (Storage.rename(bakPath.c_str(), path.c_str())) {
        LOG_INF("TODO", "Recovered daily file from interrupted save");
      }
    }
  }

  HalFile file;
  if (!Storage.openFileForRead("TODO", path.c_str(), file)) {
    return {};
  }
  const size_t sz = static_cast<size_t>(file.fileSize64());
  if (sz > kMaxDailyFileBytes) {
    LOG_ERR("TODO", "Daily file too large (%zu bytes, max %zu); ignoring", sz, kMaxDailyFileBytes);
    return {};
  }
  std::string out;
  if (sz > 0) {
    out.resize(sz);
    const int rd = file.read(&out[0], sz);
    if (rd < 0 || static_cast<size_t>(rd) != sz) {
      LOG_ERR("TODO", "Daily file read short");
      return {};
    }
  }
  return out;
}

bool writeDailyFileAtomic(const std::string& path, const std::string& content) {
  const std::string tempPath = path + ".tmp";
  const std::string backupPath = path + ".bak";

  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite("TODO", tempPath.c_str(), file)) {
    LOG_ERR("TODO", "Failed to open temp file for write: %s", tempPath.c_str());
    return false;
  }

  const size_t bytesToWrite = content.size();
  if (bytesToWrite > 0) {
    const int written = file.write(reinterpret_cast<const uint8_t*>(content.data()), bytesToWrite);
    if (written < 0 || static_cast<size_t>(written) != bytesToWrite) {
      LOG_ERR("TODO", "Short write to temp file: %s", tempPath.c_str());
      file.close();
      Storage.remove(tempPath.c_str());
      return false;
    }
  }
  file.close();

  const bool hasExisting = Storage.exists(path.c_str());
  if (Storage.exists(backupPath.c_str())) {
    Storage.remove(backupPath.c_str());
  }
  if (hasExisting && !Storage.rename(path.c_str(), backupPath.c_str())) {
    LOG_ERR("TODO", "Failed to rename existing file to backup: %s", backupPath.c_str());
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), path.c_str())) {
    LOG_ERR("TODO", "Failed to rename temp file to target: %s", path.c_str());
    if (hasExisting) {
      Storage.rename(backupPath.c_str(), path.c_str());
    }
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (hasExisting) {
    Storage.remove(backupPath.c_str());
  }
  return true;
}

std::string resolveDailyPath(const std::string& date, const bool markdownEnabled) {
  const std::string markdownPath = "/daily/" + date + ".md";
  const std::string textPath = "/daily/" + date + ".txt";
  const bool markdownExists = Storage.exists(markdownPath.c_str());
  const bool textExists = Storage.exists(textPath.c_str());
  return dailyPath(date, markdownEnabled, markdownExists, textExists);
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
  const std::string recToken = recurrenceToken(item.recurrence, item.weekdayMask);
  if (!recToken.empty()) {
    line += recToken;
    line += ' ';
  }
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

bool parseRecurrenceToken(const std::string& token, TodoRecurrence& out, uint8_t& mask) {
  // Case-insensitive comparison for the token prefix.
  std::string lower(token.size(), '\0');
  std::transform(token.begin(), token.end(), lower.begin(),
                 [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });

  if (lower == "!daily") {
    out = TodoRecurrence::Daily;
    mask = 0;
    return true;
  }
  if (lower == "!weekly") {
    out = TodoRecurrence::Weekly;
    mask = 0;
    return true;
  }
  if (lower.size() > 4 && lower.compare(0, 4, "!wk:") == 0) {
    const std::string dayList = lower.substr(4);
    if (dayList.empty()) {
      return false;
    }
    static constexpr const char* kDayNames[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    uint8_t parsedMask = 0;
    size_t pos = 0;
    while (pos < dayList.size()) {
      const size_t comma = dayList.find(',', pos);
      const std::string dayName = dayList.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
      bool found = false;
      for (int i = 0; i < 7; ++i) {
        if (dayName == kDayNames[i]) {
          parsedMask |= static_cast<uint8_t>(1u << i);
          found = true;
          break;
        }
      }
      if (!found) {
        return false;  // Unknown day name → reject the whole token.
      }
      pos = comma == std::string::npos ? dayList.size() : comma + 1;
    }
    if (parsedMask == 0) {
      return false;  // Empty after parsing → not a valid recurrence token.
    }
    out = TodoRecurrence::Weekdays;
    mask = parsedMask;
    return true;
  }
  return false;
}

std::string recurrenceToken(const TodoRecurrence recurrence, const uint8_t mask) {
  switch (recurrence) {
    case TodoRecurrence::Daily:
      return "!daily";
    case TodoRecurrence::Weekly:
      return "!weekly";
    case TodoRecurrence::Weekdays: {
      static constexpr const char* kDayNames[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
      std::string result = "!wk:";
      bool first = true;
      for (int i = 0; i < 7; ++i) {
        if (mask & (1u << i)) {
          if (!first) {
            result += ',';
          }
          result += kDayNames[i];
          first = false;
        }
      }
      return result;
    }
    case TodoRecurrence::None:
      break;
  }
  return {};
}

bool recursOn(const TodoItem& item, const int targetWeekday) {
  if (targetWeekday < 0 || targetWeekday > 6) {
    return false;
  }
  switch (item.recurrence) {
    case TodoRecurrence::None:
      return false;
    case TodoRecurrence::Daily:
      return true;
    case TodoRecurrence::Weekdays:
      return (item.weekdayMask >> targetWeekday) & 1;
    case TodoRecurrence::Weekly:
      // Bare !weekly with no mask → carry forward (see design doc).
      return item.weekdayMask == 0 ? true : static_cast<bool>((item.weekdayMask >> targetWeekday) & 1);
  }
  return false;
}

void collectRecurringDefinitions(const std::vector<TodoItem>& sourceItems, const std::string& sourceIsoDate,
                                 std::vector<TodoItem>& accumulator) {
  const int sourceWeekday = DateUtils::weekdayIndex(sourceIsoDate);
  accumulator.reserve(accumulator.size() + sourceItems.size());
  for (const TodoItem& item : sourceItems) {
    if (item.isHeader || item.recurrence == TodoRecurrence::None) {
      continue;
    }
    // Dedupe by text: the accumulator is filled most-recent-day-first, so an
    // already-present definition is the newer one and wins.
    const bool seen = std::any_of(accumulator.begin(), accumulator.end(),
                                  [&item](const TodoItem& known) { return known.text == item.text; });
    if (seen) {
      continue;
    }
    TodoItem carried = item;
    // A recurring line is a rule plus one day's instance. Harvest the rule even
    // from a completed instance — otherwise checking a !daily task off is what
    // stops it recurring — and always materialise the next day fresh.
    carried.checked = false;
    // Normalise bare !weekly → !wk:<source weekday>
    if (carried.recurrence == TodoRecurrence::Weekly && carried.weekdayMask == 0 && sourceWeekday >= 0) {
      carried.recurrence = TodoRecurrence::Weekdays;
      carried.weekdayMask = static_cast<uint8_t>(1u << sourceWeekday);
    }
    accumulator.push_back(std::move(carried));
  }
}

std::vector<TodoItem> selectDueOn(const std::vector<TodoItem>& definitions, const std::string& targetIsoDate) {
  const int targetWeekday = DateUtils::weekdayIndex(targetIsoDate);
  if (targetWeekday < 0) {
    return {};
  }
  std::vector<TodoItem> result;
  result.reserve(definitions.size());
  std::copy_if(definitions.begin(), definitions.end(), std::back_inserter(result),
               [targetWeekday](const TodoItem& item) { return recursOn(item, targetWeekday); });
  return result;
}

}  // namespace TodoPlannerStorage
