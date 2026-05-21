#pragma once

#include <string>

namespace TodoPlannerStorage {

constexpr size_t kTodoEntryMaxTextLength = 300;

// Select daily TODO file path with precedence:
// 1) Existing .md, 2) existing .txt, 3) extension from markdownEnabled fallback.
inline std::string dailyPath(const std::string& date, const bool markdownEnabled, const bool markdownExists,
                             const bool textExists) {
  if (markdownExists) {
    return "/daily/" + date + ".md";
  }
  if (textExists) {
    return "/daily/" + date + ".txt";
  }
  return "/daily/" + date + (markdownEnabled ? ".md" : ".txt");
}

// Agenda entries: blockquote ("> text") when markdown is enabled, plain text otherwise.
// TODO entries always use markdown checkbox format ("- [ ] text").
inline std::string formatEntry(const std::string& text, const bool agendaEntry, const bool markdownEnabled = false) {
  if (agendaEntry) return markdownEnabled ? "> " + text : text;
  return "- [ ] " + text;
}

}  // namespace TodoPlannerStorage
