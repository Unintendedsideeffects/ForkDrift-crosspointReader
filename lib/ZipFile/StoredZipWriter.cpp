#include "InflateStream.h"
#include "ZipFile.h"

#include <Logging.h>
#include <esp_task_wdt.h>

#include <cstring>

namespace {

constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflated = 8;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;
constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint16_t kFlagEncrypted = 0x0001;
constexpr uint16_t kFlagStrongEncrypt = 0x0040;
constexpr uint16_t kFlagUtf8 = 0x0800;
constexpr uint16_t kMaxNameLen = 255;
constexpr size_t kChunk = 1024;
constexpr uint32_t kZip32Limit = 0xFFFFFFFFu;

struct CdFields {
  uint16_t versionMadeBy = 0;
  uint16_t versionNeeded = 0;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint16_t modTime = 0;
  uint16_t modDate = 0;
  uint32_t crc = 0;
  uint32_t compressedSize = 0;
  uint32_t uncompressedSize = 0;
  uint16_t nameLen = 0;
  uint16_t extraLen = 0;
  uint16_t commentLen = 0;
  uint16_t diskStart = 0;
  uint16_t internalAttrs = 0;
  uint32_t externalAttrs = 0;
  uint32_t localHeaderOffset = 0;
};

uint32_t zipCrc32(uint32_t crc, const uint8_t* data, const uint32_t length) {
  crc ^= 0xffffffffu;
  for (uint32_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
  }
  return crc ^ 0xffffffffu;
}

bool writeExact(HalFile& file, const void* data, const size_t length) {
  if (length == 0) {
    return true;
  }
  return file.write(data, length) == length;
}

bool putU16(HalFile& file, const uint16_t value) {
  const uint8_t bytes[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  return writeExact(file, bytes, 2);
}

bool putU32(HalFile& file, const uint32_t value) {
  const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  return writeExact(file, bytes, 4);
}

bool readExact(HalFile& file, void* data, const size_t length) {
  if (length == 0) {
    return true;
  }
  return static_cast<size_t>(file.read(data, length)) == length;
}

bool readLe32(HalFile& file, uint32_t* value) { return readExact(file, value, sizeof(*value)); }

void parseCdFields(const uint8_t hdr[42], CdFields* fields) {
  memcpy(&fields->versionMadeBy, hdr + 0, 2);
  memcpy(&fields->versionNeeded, hdr + 2, 2);
  memcpy(&fields->flags, hdr + 4, 2);
  memcpy(&fields->method, hdr + 6, 2);
  memcpy(&fields->modTime, hdr + 8, 2);
  memcpy(&fields->modDate, hdr + 10, 2);
  memcpy(&fields->crc, hdr + 12, 4);
  memcpy(&fields->compressedSize, hdr + 16, 4);
  memcpy(&fields->uncompressedSize, hdr + 20, 4);
  memcpy(&fields->nameLen, hdr + 24, 2);
  memcpy(&fields->extraLen, hdr + 26, 2);
  memcpy(&fields->commentLen, hdr + 28, 2);
  memcpy(&fields->diskStart, hdr + 30, 2);
  memcpy(&fields->internalAttrs, hdr + 32, 2);
  memcpy(&fields->externalAttrs, hdr + 34, 4);
  memcpy(&fields->localHeaderOffset, hdr + 38, 4);
}

bool skipAndCrc(HalFile& file, uint32_t remaining, uint32_t* crc) {
  uint8_t buf[256];
  while (remaining > 0) {
    const uint32_t n = remaining < sizeof(buf) ? remaining : static_cast<uint32_t>(sizeof(buf));
    if (!readExact(file, buf, n)) {
      return false;
    }
    *crc = zipCrc32(*crc, buf, n);
    remaining -= n;
  }
  return true;
}

bool entryUnsupported(const CdFields& fields) {
  if ((fields.flags & kFlagEncrypted) != 0 || (fields.flags & kFlagStrongEncrypt) != 0) {
    return true;
  }
  if (fields.method != kMethodStored && fields.method != kMethodDeflated) {
    return true;
  }
  if (fields.diskStart != 0) {
    return true;
  }
  if (fields.compressedSize == kZip32Limit || fields.uncompressedSize == kZip32Limit ||
      fields.localHeaderOffset == kZip32Limit) {
    return true;
  }
  if (fields.nameLen > kMaxNameLen) {
    return true;
  }
  return false;
}

struct ZipFillCtx {
  HalFile* file = nullptr;
  uint32_t remaining = 0;
  uint8_t buf[kChunk] = {};
};

size_t zipFill(void* ctx, const uint8_t** data) {
  auto* fill = static_cast<ZipFillCtx*>(ctx);
  if (fill->remaining == 0) {
    return 0;
  }
  const size_t want = fill->remaining < sizeof(fill->buf) ? fill->remaining : sizeof(fill->buf);
  const int got = fill->file->read(fill->buf, want);
  if (got <= 0) {
    return 0;
  }
  fill->remaining -= static_cast<uint32_t>(got);
  *data = fill->buf;
  return static_cast<size_t>(got);
}

bool copyStored(HalFile& src, HalFile& dest, const uint32_t length, const uint32_t expectedCrc) {
  uint8_t buf[kChunk];
  uint32_t remaining = length;
  uint32_t crc = 0;
  while (remaining > 0) {
    const uint32_t n = remaining < sizeof(buf) ? remaining : static_cast<uint32_t>(sizeof(buf));
    if (!readExact(src, buf, n) || !writeExact(dest, buf, n)) {
      return false;
    }
    crc = zipCrc32(crc, buf, n);
    remaining -= n;
    esp_task_wdt_reset();
  }
  return crc == expectedCrc;
}

bool inflateToStored(HalFile& src, HalFile& dest, const uint32_t compressedSize, const uint32_t uncompressedSize,
                     const uint32_t expectedCrc) {
  if (uncompressedSize == 0) {
    return expectedCrc == 0 || compressedSize == 0;
  }

  InflateStream stream;
  if (!stream.init(true, InflateStream::ScratchPolicy::RequireBuildScratch)) {
    return false;
  }

  ZipFillCtx fillCtx;
  fillCtx.file = &src;
  fillCtx.remaining = compressedSize;
  stream.setFill(zipFill, &fillCtx);

  uint8_t buf[kChunk];
  uint32_t produced = 0;
  uint32_t crc = 0;
  while (produced < uncompressedSize) {
    size_t n = 0;
    const size_t want = uncompressedSize - produced < sizeof(buf) ? uncompressedSize - produced : sizeof(buf);
    const auto status = stream.readAtMost(buf, want, &n);
    if (status == InflateStream::Status::Error || n == 0) {
      return false;
    }
    if (!writeExact(dest, buf, n)) {
      return false;
    }
    crc = zipCrc32(crc, buf, static_cast<uint32_t>(n));
    produced += static_cast<uint32_t>(n);
    esp_task_wdt_reset();
  }
  return produced == uncompressedSize && crc == expectedCrc;
}

bool writeLocalHeader(HalFile& dest, const CdFields& fields, const char* name) {
  const uint16_t flags = static_cast<uint16_t>(fields.flags & kFlagUtf8);
  return putU32(dest, kLocalSig) && putU16(dest, 20) && putU16(dest, flags) && putU16(dest, kMethodStored) &&
         putU16(dest, fields.modTime) && putU16(dest, fields.modDate) && putU32(dest, fields.crc) &&
         putU32(dest, fields.uncompressedSize) && putU32(dest, fields.uncompressedSize) &&
         putU16(dest, fields.nameLen) && putU16(dest, 0) && writeExact(dest, name, fields.nameLen);
}

bool writeCentralHeader(HalFile& dest, const CdFields& fields, const char* name, const uint32_t localOffset) {
  const uint16_t flags = static_cast<uint16_t>(fields.flags & kFlagUtf8);
  return putU32(dest, kCentralSig) && putU16(dest, fields.versionMadeBy) && putU16(dest, 20) && putU16(dest, flags) &&
         putU16(dest, kMethodStored) && putU16(dest, fields.modTime) && putU16(dest, fields.modDate) &&
         putU32(dest, fields.crc) && putU32(dest, fields.uncompressedSize) && putU32(dest, fields.uncompressedSize) &&
         putU16(dest, fields.nameLen) && putU16(dest, 0) && putU16(dest, 0) && putU16(dest, 0) &&
         putU16(dest, fields.internalAttrs) && putU32(dest, fields.externalAttrs) && putU32(dest, localOffset) &&
         writeExact(dest, name, fields.nameLen);
}

}  // namespace

ZipInspect ZipFile::inspect() {
  ZipInspect result;
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return result;
  }
  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return result;
  }

  if (zipDetails.thisDisk != 0 || zipDetails.cdDisk != 0 || zipDetails.totalEntries == 0xFFFFu ||
      zipDetails.centralDirOffset == kZip32Limit || zipDetails.centralDirSize == kZip32Limit) {
    result.kind = ZipKind::Unsupported;
    if (!wasOpen) {
      close();
    }
    return result;
  }

  result.centralDirOffset = zipDetails.centralDirOffset;
  result.centralDirSize = zipDetails.centralDirSize;
  result.entryCount = zipDetails.totalEntries;
  result.storedOutputSize = 22;

  if (!file.seek(zipDetails.centralDirOffset)) {
    if (!wasOpen) {
      close();
    }
    return result;
  }

  uint32_t crc = 0;
  uint16_t counted = 0;
  bool unsupported = false;
  bool corrupt = false;
  bool hasDeflate = false;
  uint8_t hdr[42];

  for (;;) {
    uint32_t sig = 0;
    const int sigRead = static_cast<int>(file.read(&sig, sizeof(sig)));
    if (sigRead != static_cast<int>(sizeof(sig))) {
      corrupt = true;
      break;
    }
    if (sig != kCentralSig) {
      break;
    }
    crc = zipCrc32(crc, reinterpret_cast<const uint8_t*>(&sig), sizeof(sig));

    if (!readExact(file, hdr, sizeof(hdr))) {
      corrupt = true;
      break;
    }
    crc = zipCrc32(crc, hdr, sizeof(hdr));

    CdFields fields;
    parseCdFields(hdr, &fields);
    if (entryUnsupported(fields)) {
      unsupported = true;
    }
    if (fields.method == kMethodDeflated) {
      hasDeflate = true;
    }
    if (!skipAndCrc(file, static_cast<uint32_t>(fields.nameLen) + fields.extraLen + fields.commentLen, &crc)) {
      corrupt = true;
      break;
    }

    const uint64_t localBytes = 30ull + fields.nameLen + fields.uncompressedSize;
    const uint64_t centralBytes = 46ull + fields.nameLen;
    if (result.storedOutputSize > kZip32Limit - localBytes ||
        result.storedOutputSize + localBytes > kZip32Limit - centralBytes) {
      unsupported = true;
    } else {
      result.storedOutputSize += localBytes + centralBytes;
    }
    counted++;
    if (counted == 0) {
      unsupported = true;
      break;
    }
  }

  result.centralDirCrc = crc;
  result.hasDeflate = hasDeflate;
  result.entryCount = counted;
  if (corrupt) {
    result.kind = ZipKind::Corrupt;
  } else if (unsupported) {
    result.kind = ZipKind::Unsupported;
  } else if (hasDeflate) {
    result.kind = ZipKind::HasDeflate;
  } else {
    result.kind = ZipKind::StoredOnly;
  }

  if (!wasOpen) {
    close();
  }
  return result;
}

bool ZipFile::writeStoredArchive(const char* destPath, const char* offsetsPath) {
  if (destPath == nullptr || offsetsPath == nullptr) {
    return false;
  }

  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }
  if (!loadZipDetails()) {
    if (!wasOpen) {
      close();
    }
    return false;
  }

  HalFile dest;
  HalFile offsets;
  if (!Storage.openFileForWrite("ZIP", destPath, dest) || !Storage.openFileForWrite("ZIP", offsetsPath, offsets)) {
    dest.close();
    offsets.close();
    Storage.remove(destPath);
    Storage.remove(offsetsPath);
    if (!wasOpen) {
      close();
    }
    return false;
  }

  uint32_t destOffset = 0;
  uint16_t written = 0;
  char name[kMaxNameLen + 1];
  uint8_t hdr[42];
  bool ok = file.seek(zipDetails.centralDirOffset);

  while (ok) {
    uint32_t sig = 0;
    if (!readLe32(file, &sig)) {
      ok = false;
      break;
    }
    if (sig != kCentralSig) {
      break;
    }
    if (!readExact(file, hdr, sizeof(hdr))) {
      ok = false;
      break;
    }
    CdFields fields;
    parseCdFields(hdr, &fields);
    if (entryUnsupported(fields) || fields.nameLen > kMaxNameLen) {
      ok = false;
      break;
    }
    if (!readExact(file, name, fields.nameLen)) {
      ok = false;
      break;
    }
    name[fields.nameLen] = '\0';
    if (!file.seekCur(static_cast<int64_t>(fields.extraLen) + fields.commentLen)) {
      ok = false;
      break;
    }
    const uint32_t nextCd = file.position();
    FileStatSlim stat;
    stat.method = fields.method;
    stat.compressedSize = fields.compressedSize;
    stat.uncompressedSize = fields.uncompressedSize;
    stat.localHeaderOffset = fields.localHeaderOffset;
    const long payload = getDataOffset(stat);
    if (payload < 0 || !file.seek(static_cast<size_t>(payload))) {
      ok = false;
      break;
    }
    if (!writeExact(offsets, &destOffset, sizeof(destOffset))) {
      ok = false;
      break;
    }
    if (!writeLocalHeader(dest, fields, name)) {
      ok = false;
      break;
    }
    destOffset += 30u + fields.nameLen;
    if (fields.method == kMethodStored) {
      ok = copyStored(file, dest, fields.uncompressedSize, fields.crc);
    } else {
      ok = inflateToStored(file, dest, fields.compressedSize, fields.uncompressedSize, fields.crc);
    }
    if (!ok) {
      break;
    }
    destOffset += fields.uncompressedSize;
    written++;
    if (!file.seek(nextCd)) {
      ok = false;
      break;
    }
    esp_task_wdt_reset();
  }

  offsets.close();
  if (!ok) {
    dest.close();
    Storage.remove(destPath);
    Storage.remove(offsetsPath);
    if (!wasOpen) {
      close();
    }
    return false;
  }

  if (!Storage.openFileForRead("ZIP", offsetsPath, offsets) || !file.seek(zipDetails.centralDirOffset)) {
    dest.close();
    offsets.close();
    Storage.remove(destPath);
    Storage.remove(offsetsPath);
    if (!wasOpen) {
      close();
    }
    return false;
  }

  const uint32_t centralDirOffset = destOffset;
  uint16_t centralCount = 0;
  while (ok) {
    uint32_t sig = 0;
    if (!readLe32(file, &sig)) {
      ok = false;
      break;
    }
    if (sig != kCentralSig) {
      break;
    }
    if (!readExact(file, hdr, sizeof(hdr))) {
      ok = false;
      break;
    }
    CdFields fields;
    parseCdFields(hdr, &fields);
    if (!readExact(file, name, fields.nameLen)) {
      ok = false;
      break;
    }
    name[fields.nameLen] = '\0';
    if (!file.seekCur(static_cast<int64_t>(fields.extraLen) + fields.commentLen)) {
      ok = false;
      break;
    }
    uint32_t localOffset = 0;
    if (!readExact(offsets, &localOffset, sizeof(localOffset))) {
      ok = false;
      break;
    }
    if (!writeCentralHeader(dest, fields, name, localOffset)) {
      ok = false;
      break;
    }
    destOffset += 46u + fields.nameLen;
    centralCount++;
    esp_task_wdt_reset();
  }

  offsets.close();
  Storage.remove(offsetsPath);

  if (!ok || centralCount != written) {
    dest.close();
    Storage.remove(destPath);
    if (!wasOpen) {
      close();
    }
    return false;
  }

  const uint32_t centralDirSize = destOffset - centralDirOffset;
  const bool eocdOk = putU32(dest, kEocdSig) && putU16(dest, 0) && putU16(dest, 0) && putU16(dest, written) &&
                      putU16(dest, written) && putU32(dest, centralDirSize) && putU32(dest, centralDirOffset) &&
                      putU16(dest, 0) && dest.sync();
  if (!eocdOk) {
    dest.close();
    Storage.remove(destPath);
    if (!wasOpen) {
      close();
    }
    return false;
  }
  dest.close();
  if (!wasOpen) {
    close();
  }
  LOG_INF("ZIP", "Wrote STORED archive %s (%u entries, %u bytes)", destPath, static_cast<unsigned>(written),
          static_cast<unsigned>(destOffset + 22u));
  return true;
}

bool ZipFile::validateStoredArchive(const uint16_t expectedCount, const uint64_t expectedSize) {
  const ZipInspect inspected = inspect();
  if (inspected.kind != ZipKind::StoredOnly || inspected.hasDeflate) {
    return false;
  }
  if (inspected.entryCount != expectedCount || inspected.storedOutputSize != expectedSize) {
    return false;
  }
  const bool wasOpen = isOpen();
  if (!wasOpen && !open()) {
    return false;
  }
  const uint64_t size = file.fileSize64();
  if (!wasOpen) {
    close();
  }
  return size == expectedSize;
}
