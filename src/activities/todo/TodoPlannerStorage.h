#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/todo/TodoItem.h"

namespace TodoPlannerStorage {

constexpr size_t kTodoEntryMaxTextLength = 300;
constexpr int kRolloverScanBackDays = 14;
constexpr size_t kMaxDailyFileBytes = 32u * 1024u;

std::string dailyPath(const std::string& date, bool markdownEnabled, bool markdownExists, bool textExists);
std::string resolveDailyPath(const std::string& date, bool markdownEnabled);

// Reads a daily planner file, rejecting anything larger than kMaxDailyFileBytes
// (32 KB) so a single oversized /daily/<date>.md can't slurp megabytes into the
// heap. Returns {} on any failure.
// Lives here rather than in the web API because the planner activities need it
// too, and `[env:simulator]` excludes `src/network/server/`.
std::string readDailyFileCapped(const std::string& path);

// Writes a daily planner file atomically: content goes to <path>.tmp, the
// existing file is renamed to <path>.bak, the temp is renamed into place, and
// the backup is dropped. A crash at any point leaves either the old file, or a
// promotable <path>.tmp / <path>.bak — never nothing. Caller must already hold
// SpiBusMutex. Returns false on any failure, leaving the original intact.
bool writeDailyFileAtomic(const std::string& path, const std::string& content);

std::string formatEntry(const std::string& text, bool agendaEntry, bool markdownEnabled = false);

bool parseLine(std::string line, TodoItem& out);

std::string formatItem(const TodoItem& item, bool markdownFile);

void parseFile(const std::string& content, std::vector<TodoItem>& out);

std::string formatFile(const std::vector<TodoItem>& items, bool markdownFile);

// Recurrence token parsing/formatting — pure, date-free.
bool parseRecurrenceToken(const std::string& token, TodoRecurrence& out, uint8_t& mask);
std::string recurrenceToken(TodoRecurrence recurrence, uint8_t mask);

// Returns true if the item recurs on the given weekday (0=Mon..6=Sun).
bool recursOn(const TodoItem& item, int targetWeekday);

// Pure rollover logic, split in two so a recurrence definition survives days it
// is not due on. Callers walk backwards over the scan window appending each
// day's definitions (most recent day FIRST — dedupe keeps the newest), then
// filter by the target date. Collecting only due-today items per day would drop
// e.g. "!wk:mon" the moment any intermediate day got a file of its own.
void collectRecurringDefinitions(const std::vector<TodoItem>& sourceItems, const std::string& sourceIsoDate,
                                 std::vector<TodoItem>& accumulator);

std::vector<TodoItem> selectDueOn(const std::vector<TodoItem>& definitions, const std::string& targetIsoDate);

}  // namespace TodoPlannerStorage
