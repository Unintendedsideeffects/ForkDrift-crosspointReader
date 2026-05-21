#include "DateUtils.h"

#include <HalStorage.h>

#include <algorithm>
#include <cctype>
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

bool parseIsoDate(const std::string& isoDate, std::tm& out) {
  if (isoDate.size() != 10 || isoDate[4] != '-' || isoDate[7] != '-') {
    return false;
  }

  static constexpr size_t kDigitIndices[] = {0, 1, 2, 3, 5, 6, 8, 9};
  if (!std::all_of(std::begin(kDigitIndices), std::end(kDigitIndices),
                   [&](const size_t index) { return std::isdigit(static_cast<unsigned char>(isoDate[index])); })) {
    return false;
  }

  out = std::tm{};
  out.tm_year = std::stoi(isoDate.substr(0, 4)) - 1900;
  out.tm_mon = std::stoi(isoDate.substr(5, 2)) - 1;
  out.tm_mday = std::stoi(isoDate.substr(8, 2));
  out.tm_isdst = -1;
  return true;
}

std::string formatIsoDate(const std::tm& timeInfo) {
  char buffer[11] = {};
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", timeInfo.tm_year + 1900, timeInfo.tm_mon + 1,
                timeInfo.tm_mday);
  return std::string(buffer);
}

std::string offsetDate(const std::string& isoDate, const int days) {
  if (isoDate.empty()) {
    return {};
  }

  std::tm timeInfo{};
  if (!parseIsoDate(isoDate, timeInfo)) {
    return {};
  }

  timeInfo.tm_mday += days;
  if (mktime(&timeInfo) == -1) {
    return {};
  }
  return formatIsoDate(timeInfo);
}

std::string formatDayTitle(const std::string& isoDate) {
  std::tm timeInfo{};
  if (!parseIsoDate(isoDate, timeInfo)) {
    return isoDate;
  }

  char buffer[32] = {};
  if (std::strftime(buffer, sizeof(buffer), "%a · %b %d", &timeInfo) == 0) {
    return isoDate;
  }
  return std::string(buffer);
}

std::string formatDayIndexLabel(const std::string& isoDate) {
  std::tm timeInfo{};
  if (!parseIsoDate(isoDate, timeInfo)) {
    return isoDate;
  }

  char buffer[16] = {};
  if (std::strftime(buffer, sizeof(buffer), "%a %d", &timeInfo) == 0) {
    return isoDate;
  }
  return std::string(buffer);
}

bool dailyFileExists(const std::string& date, const bool markdownEnabled) {
  if (date.empty()) {
    return false;
  }

  const std::string markdownPath = "/daily/" + date + ".md";
  const std::string textPath = "/daily/" + date + ".txt";
  const bool markdownExists = Storage.exists(markdownPath.c_str());
  const bool textExists = Storage.exists(textPath.c_str());
  if (markdownExists || textExists) {
    return true;
  }
  (void)markdownEnabled;
  return false;
}
}  // namespace DateUtils
