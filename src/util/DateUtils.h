#pragma once

#include <string>

namespace DateUtils {
// Returns current date in YYYY-MM-DD format.
// Returns empty string if system time is not set.
std::string currentDate();

// Returns a 24-hour Roman-numeral clock label rounded down to the current quarter hour.
// Returns empty string if system time is not set.
std::string currentClockLabel();

// Returns a precise digital clock label ("HH:MM", or "H:MM AM/PM" when
// SETTINGS.clockFormat selects 12-hour). Prefers the DS3231 RTC when present
// (X3 — survives deep sleep / power loss); otherwise falls back to system time.
// Both sources use the same timezone-offset setting as the Roman clock.
// Returns empty string if no time source is available.
std::string currentDigitalClockLabel();

bool getHourAndMinute(int& hour, int& minute);

std::string offsetDate(const std::string& isoDate, int days);

std::string formatDayTitle(const std::string& isoDate);

std::string formatDayIndexLabel(const std::string& isoDate);

// Returns 0=Mon .. 6=Sun for an ISO "YYYY-MM-DD" date, or -1 if unparseable.
int weekdayIndex(const std::string& isoDate);

bool dailyFileExists(const std::string& date, bool markdownEnabled);
}  // namespace DateUtils
