#pragma once

#ifndef CROSSPOINT_HOST_BUILD
#include <HardwareSerial.h>
#else
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#endif

#include <string>

// Registered by the storage layer after it has initialized. Logging remains a
// leaf library: it never includes or calls the storage implementation directly.
using DeveloperLogAppendFn = void (*)(const char* message, size_t length);
void setDeveloperLogAppendFn(DeveloperLogAppendFn appendFn);

/*
Define ENABLE_SERIAL_LOG to enable logging
Can be set in platformio.ini build_flags or as a compile definition

Define LOG_LEVEL to control log verbosity:
0 = ERR only
1 = ERR + INF
2 = ERR + INF + DBG
If not defined, defaults to 0

If you have a legitimate need for raw Serial access (e.g., binary data,
special formatting), use the underlying logSerial object directly:
    logSerial.printf("Special case: %d\n", value);
    logSerial.write(binaryData, length);

The logSerial reference (defined below) points to the real Serial object and
won't trigger deprecation warnings.
*/

#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

#ifndef CROSSPOINT_HOST_BUILD
static HWCDC& logSerial = Serial;
#else
struct HostSerial {
  template <typename T>
  size_t print(T val) {
    return printf("%s", std::to_string(val).c_str());
  }
  size_t print(const char* val) { return printf("%s", val); }
  size_t print(char* val) { return printf("%s", val); }
  void flush() {}
  operator bool() const { return true; }
};
static HostSerial logSerial;
#endif

void logPrintf(const char* level, const char* origin, const char* format, ...);
bool isDeveloperModeLoggingEnabled();
void setDeveloperModeLoggingEnabled(bool enabled);

// Gates ONLY the serial (CDC) output of LOG_* — the crash-report ring buffer
// and developer SD log keep receiving entries. Used by the USB serial JSON-RPC
// protocol, which shares the CDC stream with logging: stray log lines corrupt
// in-flight JSON responses. Written from the main loop only; other tasks read
// it without synchronization (a torn read costs at most one stray log line).
bool isSerialLogSuppressed();
void setSerialLogSuppressed(bool suppressed);

#ifdef ENABLE_SERIAL_LOG
#if LOG_LEVEL >= 0
#define LOG_ERR(origin, format, ...) logPrintf("ERR", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_ERR(origin, format, ...)
#endif

#define LOG_INF(origin, format, ...)                         \
  do {                                                       \
    if (LOG_LEVEL >= 1 || isDeveloperModeLoggingEnabled()) { \
      logPrintf("INF", origin, format "\n", ##__VA_ARGS__);  \
    }                                                        \
  } while (0)

#define LOG_WRN(origin, format, ...)                          \
  do {                                                        \
    if (LOG_LEVEL >= 1 || isDeveloperModeLoggingEnabled()) {  \
      logPrintf("[WRN]", origin, format "\n", ##__VA_ARGS__); \
    }                                                         \
  } while (0)

#define LOG_DBG(origin, format, ...)                         \
  do {                                                       \
    if (LOG_LEVEL >= 2 || isDeveloperModeLoggingEnabled()) { \
      logPrintf("DBG", origin, format "\n", ##__VA_ARGS__);  \
    }                                                        \
  } while (0)
#else
#define LOG_DBG(origin, format, ...)
#define LOG_ERR(origin, format, ...)
#define LOG_INF(origin, format, ...)
#define LOG_WRN(origin, format, ...)
#endif

std::string getLastLogs();
void clearLastLogs();
// Validates the RTC log state (magic word + logHead range). Returns true if
// corruption was detected (magic mismatch or logHead out of range), meaning
// logMessages is untrusted garbage. Callers should call clearLastLogs() when
// this returns true so getLastLogs() does not dump corrupt data into crash reports.
bool sanitizeLogHead();

#if !defined(HOST_BUILD) && !defined(CROSSPOINT_HOST_BUILD)

class MySerialImpl : public Print {
 public:
  void begin(unsigned long baud) {
#ifndef SIMULATOR
    // USB serial JSON-RPC lines reach ~4.2KB (ota_chunk, see
    // UsbSerialProtocol.cpp); HWCDC's default RX buffer (256B) drops inbound
    // bytes whenever the main loop can't drain promptly — an e-ink render
    // blocks it for seconds. 8KB holds one outstanding protocol line with
    // headroom. Must be called before begin() to take effect. Only costs
    // DRAM when USB is connected at boot (the only path that calls begin()).
    logSerial.setRxBufferSize(8192);
#endif
    logSerial.begin(baud);
  }

  // Support boolean conversion for compatibility with code like:

  //   if (Serial) or while (!Serial)

  operator bool() const { return logSerial; }

  __attribute__((deprecated("Use LOG_* macro instead"))) size_t printf(const char* format, ...);

  size_t write(uint8_t b) override;

  size_t write(const uint8_t* buffer, size_t size) override;

  void flush() override;

  static MySerialImpl instance;
};

#ifdef Serial
#undef Serial
#endif
#define Serial MySerialImpl::instance

#endif
