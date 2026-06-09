#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {
template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static bool readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
  return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

template <typename T>
static bool readPod(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

static bool readString(std::istream& is, std::string& s) {
  uint32_t len;
  if (!readPod(is, len)) return false;
  if (len > 65536) return false;  // Sanity check: max 64KB for metadata strings
  if (len == 0) {
    s.clear();
    return true;
  }
  s.resize(len);
  is.read(&s[0], len);
  return is.gcount() == static_cast<std::streamsize>(len);
}

static bool readString(HalFile& file, std::string& s) {
  uint32_t len;
  if (!readPod(file, len)) return false;
  if (len > 65536) return false;  // Sanity check: max 64KB for metadata strings
  if (len == 0) {
    s.clear();
    return true;
  }
  s.resize(len);
  return file.read(reinterpret_cast<uint8_t*>(&s[0]), len) == len;
}
}  // namespace serialization
