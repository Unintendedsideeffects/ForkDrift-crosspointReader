#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <thread>

#ifndef PROGMEM
#define PROGMEM
#endif

template <typename T>
inline uint8_t pgm_read_byte(const T* ptr) {
  return static_cast<uint8_t>(*ptr);
}

constexpr uint8_t HIGH = 1;
constexpr uint8_t LOW = 0;
constexpr uint8_t INPUT = 0;
constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT_PULLUP = 2;

using byte = uint8_t;

class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const char* value) : std::string(value ? value : "") {}
  String(const std::string& value) : std::string(value) {}
  String(int value) : std::string(std::to_string(value)) {}
  String(unsigned int value) : std::string(std::to_string(value)) {}
  String(size_t value) : std::string(std::to_string(value)) {}

  bool isEmpty() const { return empty(); }
  int indexOf(char needle) const {
    const auto pos = find(needle);
    return pos == npos ? -1 : static_cast<int>(pos);
  }
  String substring(size_t start) const { return start >= size() ? String("") : String(std::string::substr(start)); }
  String substring(size_t start, size_t end) const {
    if (start >= size() || end <= start) return String("");
    return String(std::string::substr(start, end - start));
  }
  bool startsWith(const char* prefix) const { return prefix != nullptr && rfind(prefix, 0) == 0; }
  void replace(char from, char to) {
    for (char& c : *this) {
      if (c == from) c = to;
    }
  }
};

inline String operator+(const char* left, const String& right) { return String(left) + static_cast<const std::string&>(right); }

class Print {
 public:
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) {
      if (write(*buffer++))
        n++;
      else
        break;
    }
    return n;
  }
  virtual void flush() {}
};

inline unsigned long millis() {
  static const auto kStart = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - kStart;
  return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

inline unsigned long micros() {
  static const auto kStart = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - kStart;
  return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

inline void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline void yield() { std::this_thread::yield(); }

inline void pinMode(int /*pin*/, int /*mode*/) {}

inline void digitalWrite(int /*pin*/, int /*value*/) {}

inline int digitalRead(int /*pin*/) { return LOW; }

class HardwareSerial {
 public:
  operator bool() const { return true; }

  void printf(const char* /*fmt*/, ...) const {
    // Intentionally silent for deterministic host harness output.
  }

  void println(const char* /*text*/) const {
    // Intentionally silent for deterministic host harness output.
  }

  size_t print(const char* /*text*/) const { return 0; }

  size_t write(uint8_t) { return 0; }
  size_t write(const uint8_t*, size_t) { return 0; }

  void begin(unsigned long) {}
  void flush() {}
};

using HWCDC = HardwareSerial;

extern HardwareSerial Serial;

class EspClass {
 public:
  void restart() const {}
};

extern EspClass ESP;
