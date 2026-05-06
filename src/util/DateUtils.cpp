#include "DateUtils.h"

#include <cstdio>
#include <ctime>
#include <string>

#include "CrossPointSettings.h"

namespace DateUtils {
namespace {
constexpr std::time_t kMinValidTime = 1577836800;  // 2020-01-01 00:00:00 UTC

bool getAdjustedTime(std::tm& timeInfo) {
  std::time_t now = std::time(nullptr);
  if (now <= 0) {
    return false;
  }

  // Treat very old epochs as "time not set".
  if (now < kMinValidTime) {
    return false;
  }

  const auto mode = static_cast<CrossPointSettings::TIME_MODE>(SETTINGS.timeMode);
  if (mode == CrossPointSettings::TIME_MODE_LOCAL) {
    now += SETTINGS.getTimeZoneOffsetSeconds();
  }

  return gmtime_r(&now, &timeInfo) != nullptr;
}

const char* romanHour(const int hour24) {
  static constexpr const char* kHours[] = {"XXIV", "I",    "II",    "III", "IV",  "V",    "VI",   "VII",
                                           "VIII", "IX",   "X",     "XI",  "XII", "XIII", "XIV",  "XV",
                                           "XVI",  "XVII", "XVIII", "XIX", "XX",  "XXI",  "XXII", "XXIII"};
  return kHours[hour24 % 24];
}

const char* romanQuarterMinute(const int minute) {
  switch ((minute / 15) * 15) {
    case 15:
      return "I";
    case 30:
      return "II";
    case 45:
      return "III";
    case 0:
    default:
      return "";
  }
}
}  // namespace

std::string currentDate() {
  std::tm timeInfo{};
  if (!getAdjustedTime(timeInfo)) {
    return {};
  }

  char buffer[11] = {};
  const int year = timeInfo.tm_year + 1900;
  const int month = timeInfo.tm_mon + 1;
  const int day = timeInfo.tm_mday;
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  return std::string(buffer);
}

std::string currentClockLabel() {
  std::tm timeInfo{};
  if (!getAdjustedTime(timeInfo)) {
    return {};
  }

  const char* hour = romanHour(timeInfo.tm_hour);
  const char* minute = romanQuarterMinute(timeInfo.tm_min);
  char buffer[12] = {};
  if (minute[0] == '\0') {
    std::snprintf(buffer, sizeof(buffer), "%s", hour);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%s:%s", hour, minute);
  }
  return std::string(buffer);
}
}  // namespace DateUtils
