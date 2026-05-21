#pragma once

#include <string>

namespace DateUtils {
// Returns current date in YYYY-MM-DD format.
// Returns empty string if system time is not set.
std::string currentDate();

// Returns a 24-hour Roman-numeral clock label rounded down to the current quarter hour.
// Returns empty string if system time is not set.
std::string currentClockLabel();

std::string offsetDate(const std::string& isoDate, int days);

std::string formatDayTitle(const std::string& isoDate);

std::string formatDayIndexLabel(const std::string& isoDate);

bool dailyFileExists(const std::string& date, bool markdownEnabled);
}  // namespace DateUtils
