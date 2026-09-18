#include "Epub/StoredEpubCache.h"

#include <BookCachePath.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ZipFile.h>

#include <cstring>
#include <string>

namespace {

constexpr const char* kDir = "/.crosspoint/stored-epub";
constexpr uint16_t kSchema = 1;
constexpr uint64_t kFsReserveBytes = 256ull * 1024ull;
constexpr char kMagic[6] = {'S', 'T', 'O', 'R', '1', '\0'};

struct Paths {
  std::string epub;
  std::string tmp;
  std::string ready;
  std::string readyTmp;
  std::string offsets;
};

struct Fingerprint {
  uint64_t size = 0;
  uint16_t date = 0;
  uint16_t time = 0;
  uint32_t cdCrc = 0;
  uint16_t count = 0;
};

bool writeExact(HalFile& file, const void* data, const size_t length) {
  if (length == 0) {
    return true;
  }
  return file.write(data, length) == length;
}

bool readExact(HalFile& file, void* data, const size_t length) {
  if (length == 0) {
    return true;
  }
  return static_cast<size_t>(file.read(data, length)) == length;
}

bool writeU16(HalFile& file, const uint16_t value) {
  const uint8_t bytes[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  return writeExact(file, bytes, 2);
}

bool writeU32(HalFile& file, const uint32_t value) {
  const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  return writeExact(file, bytes, 4);
}

bool writeU64(HalFile& file, const uint64_t value) {
  return writeU32(file, static_cast<uint32_t>(value)) && writeU32(file, static_cast<uint32_t>(value >> 32));
}

bool readU16(HalFile& file, uint16_t* value) { return readExact(file, value, sizeof(*value)); }

bool readU32(HalFile& file, uint32_t* value) { return readExact(file, value, sizeof(*value)); }

bool readU64(HalFile& file, uint64_t* value) {
  uint32_t lo = 0;
  uint32_t hi = 0;
  if (!readU32(file, &lo) || !readU32(file, &hi)) {
    return false;
  }
  *value = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
  return true;
}

Paths pathsFor(const char* originalPath) {
  Paths paths;
  const std::string base = std::string(kDir) + "/" + std::to_string(BookCachePath::stableHash(originalPath));
  paths.epub = base + ".epub";
  paths.tmp = base + ".tmp";
  paths.ready = base + ".ready";
  paths.readyTmp = base + ".ready.tmp";
  paths.offsets = base + ".off";
  return paths;
}

void removeDerived(const Paths& paths) {
  Storage.remove(paths.ready.c_str());
  Storage.remove(paths.readyTmp.c_str());
  Storage.remove(paths.epub.c_str());
  Storage.remove(paths.tmp.c_str());
  Storage.remove(paths.offsets.c_str());
}

bool fingerprintSource(const char* originalPath, const ZipInspect& inspect, Fingerprint* out) {
  HalFile file;
  if (!Storage.openFileForRead("STOR", originalPath, file)) {
    return false;
  }
  out->size = file.fileSize64();
  if (!file.getModifyDateTime(&out->date, &out->time)) {
    out->date = 0;
    out->time = 0;
  }
  file.close();
  out->cdCrc = inspect.centralDirCrc;
  out->count = inspect.entryCount;
  return true;
}

bool writeReadyMarker(const char* path, const char* originalPath, const Fingerprint& source, const ZipInspect& output) {
  HalFile file;
  if (!Storage.openFileForWrite("STOR", path, file)) {
    return false;
  }
  const uint16_t pathLen = static_cast<uint16_t>(std::strlen(originalPath));
  const bool ok = writeExact(file, kMagic, sizeof(kMagic)) && writeU16(file, kSchema) && writeU64(file, source.size) &&
                  writeU16(file, source.date) && writeU16(file, source.time) && writeU32(file, source.cdCrc) &&
                  writeU16(file, source.count) && writeU64(file, output.storedOutputSize) &&
                  writeU16(file, output.entryCount) && writeU32(file, output.centralDirCrc) && writeU16(file, pathLen) &&
                  writeExact(file, originalPath, pathLen) && file.sync();
  file.close();
  return ok;
}

bool readReadyMarker(const char* path, const char* originalPath, const Fingerprint& source, uint64_t* outputSize,
                     uint16_t* outputCount) {
  if (!Storage.exists(path)) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("STOR", path, file)) {
    return false;
  }
  char magic[6] = {};
  uint16_t schema = 0;
  Fingerprint stored;
  uint64_t storedOutputSize = 0;
  uint16_t storedOutputCount = 0;
  uint32_t storedOutputCrc = 0;
  uint16_t pathLen = 0;
  const bool headerOk = readExact(file, magic, sizeof(magic)) && std::memcmp(magic, kMagic, sizeof(kMagic)) == 0 &&
                        readU16(file, &schema) && schema == kSchema && readU64(file, &stored.size) &&
                        readU16(file, &stored.date) && readU16(file, &stored.time) && readU32(file, &stored.cdCrc) &&
                        readU16(file, &stored.count) && readU64(file, &storedOutputSize) &&
                        readU16(file, &storedOutputCount) && readU32(file, &storedOutputCrc) &&
                        readU16(file, &pathLen);
  if (!headerOk) {
    file.close();
    return false;
  }
  std::string storedPath(pathLen, '\0');
  if (pathLen > 0 && !readExact(file, storedPath.data(), pathLen)) {
    file.close();
    return false;
  }
  file.close();
  if (storedPath != originalPath || stored.size != source.size || stored.date != source.date ||
      stored.time != source.time || stored.cdCrc != source.cdCrc || stored.count != source.count) {
    return false;
  }
  *outputSize = storedOutputSize;
  *outputCount = storedOutputCount;
  return true;
}

bool shadowMatches(const Paths& paths, const char* originalPath, const Fingerprint& source) {
  uint64_t outputSize = 0;
  uint16_t outputCount = 0;
  if (!readReadyMarker(paths.ready.c_str(), originalPath, source, &outputSize, &outputCount)) {
    return false;
  }
  if (!Storage.exists(paths.epub.c_str())) {
    return false;
  }
  ZipFile shadow(paths.epub);
  return shadow.validateStoredArchive(outputCount, outputSize);
}

}  // namespace

namespace stored_epub {

std::string select(const char* originalPath) {
  if (originalPath == nullptr || originalPath[0] == '\0') {
    return {};
  }
  const std::string identity(originalPath);
  ZipFile original(identity);
  const ZipInspect inspect = original.inspect();
  if (inspect.kind == ZipKind::Corrupt) {
    return {};
  }
  Fingerprint source;
  if (!fingerprintSource(originalPath, inspect, &source)) {
    return {};
  }
  const Paths paths = pathsFor(originalPath);
  if (shadowMatches(paths, originalPath, source)) {
    return paths.epub;
  }
  return {};
}

std::string prepare(const char* originalPath) {
  if (originalPath == nullptr || originalPath[0] == '\0') {
    return {};
  }
  const std::string identity(originalPath);
  ZipFile original(identity);
  const ZipInspect inspect = original.inspect();
  if (inspect.kind == ZipKind::Corrupt) {
    LOG_WRN("STOR", "Inspect failed, using original: %s", originalPath);
    return identity;
  }
  if (inspect.kind == ZipKind::StoredOnly) {
    LOG_INF("STOR", "Original already STORED: %s", originalPath);
    return identity;
  }
  if (inspect.kind == ZipKind::Unsupported) {
    LOG_WRN("STOR", "Unsupported ZIP, using original: %s", originalPath);
    return identity;
  }

  Fingerprint source;
  if (!fingerprintSource(originalPath, inspect, &source)) {
    LOG_WRN("STOR", "Source fingerprint failed, using original: %s", originalPath);
    return identity;
  }

  const Paths paths = pathsFor(originalPath);
  if (shadowMatches(paths, originalPath, source)) {
    LOG_INF("STOR", "Using existing shadow: %s", paths.epub.c_str());
    return paths.epub;
  }

  removeDerived(paths);
  if (!Storage.mkdir(kDir)) {
    LOG_WRN("STOR", "Cannot create %s, using original", kDir);
    return identity;
  }

  const uint64_t freeBytes = Storage.freeBytes();
  const uint64_t needed = inspect.storedOutputSize + kFsReserveBytes;
  if (freeBytes == 0 || freeBytes < needed) {
    LOG_WRN("STOR", "Not enough space (%llu < %llu), using original: %s", static_cast<unsigned long long>(freeBytes),
            static_cast<unsigned long long>(needed), originalPath);
    return identity;
  }

  LOG_INF("STOR", "Converting %s (%u entries, %llu bytes)", originalPath, static_cast<unsigned>(inspect.entryCount),
          static_cast<unsigned long long>(inspect.storedOutputSize));
  if (!original.writeStoredArchive(paths.tmp.c_str(), paths.offsets.c_str())) {
    LOG_WRN("STOR", "Convert failed, using original: %s", originalPath);
    removeDerived(paths);
    return identity;
  }

  ZipFile tmp(paths.tmp);
  if (!tmp.validateStoredArchive(inspect.entryCount, inspect.storedOutputSize)) {
    LOG_WRN("STOR", "Validate failed, using original: %s", originalPath);
    removeDerived(paths);
    return identity;
  }

  if (!Storage.rename(paths.tmp.c_str(), paths.epub.c_str())) {
    LOG_WRN("STOR", "Rename shadow failed, using original: %s", originalPath);
    removeDerived(paths);
    return identity;
  }

  const ZipInspect output = ZipFile(paths.epub).inspect();
  if (!writeReadyMarker(paths.readyTmp.c_str(), originalPath, source, output) ||
      !Storage.rename(paths.readyTmp.c_str(), paths.ready.c_str())) {
    LOG_WRN("STOR", "Ready marker failed, using original: %s", originalPath);
    removeDerived(paths);
    return identity;
  }

  LOG_INF("STOR", "Shadow ready: %s", paths.epub.c_str());
  return paths.epub;
}

void invalidate(const char* originalPath) {
  if (originalPath == nullptr || originalPath[0] == '\0') {
    return;
  }
  removeDerived(pathsFor(originalPath));
}

}  // namespace stored_epub
