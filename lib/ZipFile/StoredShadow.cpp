#include "StoredShadow.h"

#include <HalStorage.h>

#include <cstring>

namespace {

void putU16(HalFile& file, const uint16_t value) {
  const uint8_t bytes[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  file.write(bytes, 2);
}

void putU32(HalFile& file, const uint32_t value) {
  const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  file.write(bytes, 4);
}

uint32_t crc32Zip(const uint8_t* data, const uint32_t length) {
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
  }
  return crc ^ 0xffffffffu;
}

bool writeLocalHeader(HalFile& file, const StoredShadowEntry& entry, const uint32_t crc) {
  const uint16_t nameLen = static_cast<uint16_t>(std::strlen(entry.name));
  putU32(file, 0x04034b50u);
  putU16(file, 20);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, 0);
  putU32(file, crc);
  putU32(file, entry.size);
  putU32(file, entry.size);
  putU16(file, nameLen);
  putU16(file, 0);
  if (file.write(reinterpret_cast<const uint8_t*>(entry.name), nameLen) != nameLen) {
    return false;
  }
  if (entry.size > 0 && file.write(entry.data, entry.size) != entry.size) {
    return false;
  }
  return true;
}

bool writeCentralHeader(HalFile& file, const StoredShadowEntry& entry, const uint32_t crc,
                        const uint32_t localHeaderOffset) {
  const uint16_t nameLen = static_cast<uint16_t>(std::strlen(entry.name));
  putU32(file, 0x02014b50u);
  putU16(file, 0x0314);
  putU16(file, 20);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, 0);
  putU32(file, crc);
  putU32(file, entry.size);
  putU32(file, entry.size);
  putU16(file, nameLen);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, 0);
  putU32(file, 0);
  putU32(file, localHeaderOffset);
  return file.write(reinterpret_cast<const uint8_t*>(entry.name), nameLen) == nameLen;
}

}  // namespace

namespace stored_shadow {

std::string pathBesideCache(const std::string& cachePath) { return cachePath + "/stored.epub"; }

bool writeArchive(const char* path, const StoredShadowEntry* entries, const int count) {
  if (path == nullptr || entries == nullptr || count <= 0) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (entries[i].name == nullptr || (entries[i].size > 0 && entries[i].data == nullptr)) {
      return false;
    }
    if (std::strlen(entries[i].name) > 0xffffu) {
      return false;
    }
  }

  HalFile file;
  if (!Storage.openFileForWrite("ZIP", path, file)) {
    return false;
  }

  uint32_t offset = 0;
  uint32_t localOffsets[16];
  uint32_t crcs[16];
  if (count > 16) {
    file.close();
    Storage.remove(path);
    return false;
  }

  for (int i = 0; i < count; ++i) {
    localOffsets[i] = offset;
    crcs[i] = crc32Zip(entries[i].data, entries[i].size);
    if (!writeLocalHeader(file, entries[i], crcs[i])) {
      file.close();
      Storage.remove(path);
      return false;
    }
    offset += 30u + static_cast<uint32_t>(std::strlen(entries[i].name)) + entries[i].size;
  }

  const uint32_t centralDirOffset = offset;
  for (int i = 0; i < count; ++i) {
    if (!writeCentralHeader(file, entries[i], crcs[i], localOffsets[i])) {
      file.close();
      Storage.remove(path);
      return false;
    }
    offset += 46u + static_cast<uint32_t>(std::strlen(entries[i].name));
  }

  const uint32_t centralDirSize = offset - centralDirOffset;
  putU32(file, 0x06054b50u);
  putU16(file, 0);
  putU16(file, 0);
  putU16(file, static_cast<uint16_t>(count));
  putU16(file, static_cast<uint16_t>(count));
  putU32(file, centralDirSize);
  putU32(file, centralDirOffset);
  putU16(file, 0);
  file.close();
  return true;
}

}  // namespace stored_shadow
