#pragma once
#include <cstdarg>
#include <cstdio>

inline void logPrintf(const char* level, const char* origin, const char* format, ...) {
  printf("%s [%s] ", level, origin);
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

#define LOG_ERR(origin, format, ...) logPrintf("[ERR]", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) logPrintf("[INF]", origin, format, ##__VA_ARGS__)
#define LOG_WRN(origin, format, ...) logPrintf("[WRN]", origin, format, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) logPrintf("[DBG]", origin, format, ##__VA_ARGS__)

inline bool isDeveloperModeLoggingEnabled() { return false; }
inline void setDeveloperModeLoggingEnabled(bool enabled) { (void)enabled; }
