#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateReader.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

struct ZipInflateCtx {
  InflateReader reader;  // Must be first — callback casts uzlib_uncomp* to ZipInflateCtx*
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
constexpr uint32_t ZIP_CENTRAL_DIR_SIG = 0x02014b50;
constexpr size_t CENTRAL_HEADER_BYTES = 46;

uint16_t readLe16(const uint8_t* bytes) {
  uint16_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

uint32_t readLe32(const uint8_t* bytes) {
  uint32_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

bool readCentralHeader(HalFile& file, uint8_t header[CENTRAL_HEADER_BYTES]) {
  const int headerRead = static_cast<int>(file.read(header, CENTRAL_HEADER_BYTES));
  return headerRead == static_cast<int>(CENTRAL_HEADER_BYTES) && readLe32(header) == ZIP_CENTRAL_DIR_SIG;
}

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

int zipReadCallback(uzlib_uncomp* uncomp) {
  auto* ctx = reinterpret_cast<ZipInflateCtx*>(uncomp);
  if (ctx->fileRemaining == 0) return -1;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const int result = ctx->file->read(ctx->readBuf, toRead);
  // HalFile::read() returns a negative int on error. Treat it as end-of-stream
  // rather than letting the negative-to-size_t conversion underflow fileRemaining
  // and report a huge bytesRead, which would have the inflate library read past
  // the end of readBuf.
  if (result < 0) {
    LOG_ERR("ZIP", "Failed to read compressed data: %d", result);
    return -1;
  }
  const size_t bytesRead = static_cast<size_t>(result);
  ctx->fileRemaining -= bytesRead;

  if (bytesRead == 0) return -1;

  uncomp->source = ctx->readBuf + 1;
  uncomp->source_limit = ctx->readBuf + bytesRead;
  return ctx->readBuf[0];
}
}  // namespace

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  file.seek(zipDetails.centralDirOffset);

  char itemName[256];
  uint8_t header[CENTRAL_HEADER_BYTES];
  fileStatSlimCache.clear();
  // totalEntries comes straight from the (possibly corrupt) EOCD record. reserve()
  // preallocates the bucket array up front, so a bogus 65535 would grab ~256KB on a
  // 380KB device before a single entry exists. Cap the hint; the map still grows on
  // demand for the rare legitimate archive with more entries.
  constexpr uint16_t kMaxReserveEntries = 4096;
  fileStatSlimCache.reserve(zipDetails.totalEntries < kMaxReserveEntries ? zipDetails.totalEntries
                                                                         : kMaxReserveEntries);

  while (file.available()) {
    if (!readCentralHeader(file, header)) break;

    FileStatSlim fileStat = {};
    fileStat.method = readLe16(header + 10);
    fileStat.compressedSize = readLe32(header + 20);
    fileStat.uncompressedSize = readLe32(header + 24);
    const uint16_t nameLen = readLe16(header + 28);
    const uint16_t extraLen = readLe16(header + 30);
    const uint16_t commentLen = readLe16(header + 32);
    fileStat.localHeaderOffset = readLe32(header + 42);

    if (nameLen < sizeof(itemName)) {
      const int nameRead = static_cast<int>(file.read(itemName, nameLen));
      if (nameRead != static_cast<int>(nameLen)) break;
      itemName[nameLen] = '\0';
      fileStatSlimCache.emplace(itemName, fileStat);
    } else {
      file.seekCur(nameLen);
    }

    file.seekCur(static_cast<int32_t>(extraLen) + commentLen);
  }

  // Set cursor to start of central directory for sequential access
  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;

  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  const uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  uint32_t entryStart = startPos;
  bool wrapped = false;
  file.seek(entryStart);

  const size_t targetLen = strlen(filename);
  uint8_t header[CENTRAL_HEADER_BYTES];
  char itemName[256];

  while (true) {
    if (wrapped && entryStart >= startPos) break;

    if (!readCentralHeader(file, header)) {
      if (!wrapped && startPos != zipDetails.centralDirOffset) {
        entryStart = zipDetails.centralDirOffset;
        file.seek(entryStart);
        wrapped = true;
        continue;
      }
      break;
    }

    const uint16_t nameLen = readLe16(header + 28);
    const uint16_t extraLen = readLe16(header + 30);
    const uint16_t commentLen = readLe16(header + 32);
    const uint32_t nextEntry = entryStart + sizeof(header) + nameLen + extraLen + commentLen;

    if (nameLen == targetLen && nameLen < sizeof(itemName)) {
      const int nameRead = static_cast<int>(file.read(itemName, nameLen));
      if (nameRead != static_cast<int>(nameLen)) {
        LOG_ERR("ZIP", "Failed to read entry name for %s: expected %u bytes, got %d", filename,
                static_cast<unsigned int>(nameLen), nameRead);
        break;
      }
      if (memcmp(itemName, filename, nameLen) == 0) {
        fileStat->method = readLe16(header + 10);
        fileStat->compressedSize = readLe32(header + 20);
        fileStat->uncompressedSize = readLe32(header + 24);
        fileStat->localHeaderOffset = readLe32(header + 42);
        lastCentralDirPos = nextEntry;
        lastCentralDirPosValid = true;
        return true;
      }
    }

    entryStart = nextEntry;
    file.seek(entryStart);
  }

  return false;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  file.seek(fileOffset);
  const size_t read = file.read(pLocalHeader, localHeaderSize);

  if (read != localHeaderSize) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* ZIP local file header signature */) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = static_cast<uint8_t*>(malloc(scanRange));
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }

  if (!file.seek(fileSize - scanRange) || file.read(buffer, scanRange) != scanRange) {
    LOG_ERR("ZIP", "Failed to read ZIP end-of-directory record");
    free(buffer);
    return false;
  }

  // Scan backwards for the signature
  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint32_t signature = 0x06054b50;
    // &buffer[i] is unaligned for 3 of every 4 offsets; a direct uint32_t load
    // faults on RISC-V (ESP32-C3). memcpy the candidate word out instead.
    uint32_t candidate;
    memcpy(&candidate, &buffer[i], sizeof(candidate));
    if (candidate == signature) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    free(buffer);
    return false;
  }

  // Now extract the values we need from the EOCD record
  // Relative positions within EOCD:
  // Offset 10: Total number of entries (2 bytes)
  // Offset 16: Offset of start of central directory with respect to the starting disk number (4 bytes)
  // foundOffset is found by scanning arbitrary ZIP bytes, so &buffer[foundOffset+N]
  // is rarely 2/4-byte aligned. A direct reinterpret_cast load faults on RISC-V;
  // memcpy the fields out instead.
  memcpy(&zipDetails.thisDisk, &buffer[foundOffset + 4], sizeof(zipDetails.thisDisk));
  memcpy(&zipDetails.cdDisk, &buffer[foundOffset + 6], sizeof(zipDetails.cdDisk));
  memcpy(&zipDetails.totalEntries, &buffer[foundOffset + 10], sizeof(zipDetails.totalEntries));
  memcpy(&zipDetails.centralDirSize, &buffer[foundOffset + 12], sizeof(zipDetails.centralDirSize));
  memcpy(&zipDetails.centralDirOffset, &buffer[foundOffset + 16], sizeof(zipDetails.centralDirOffset));
  zipDetails.isSet = true;

  free(buffer);
  return true;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
  if (targets.empty()) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  file.seek(zipDetails.centralDirOffset);

  int matched = 0;
  const int targetCount = static_cast<int>(targets.size());
  uint8_t header[CENTRAL_HEADER_BYTES];
  char itemName[256];

  while (file.available()) {
    if (!readCentralHeader(file, header)) break;

    const uint32_t uncompressedSize = readLe32(header + 24);
    const uint16_t nameLen = readLe16(header + 28);
    const uint16_t extraLen = readLe16(header + 30);
    const uint16_t commentLen = readLe16(header + 32);

    if (nameLen < sizeof(itemName)) {
      const int nameRead = static_cast<int>(file.read(itemName, nameLen));
      if (nameRead != static_cast<int>(nameLen)) break;
      itemName[nameLen] = '\0';

      uint64_t hash = fnvHash64(itemName, nameLen);
      SizeTarget key = {hash, nameLen, 0};

      auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });

      while (it != targets.end() && it->hash == hash && it->len == nameLen) {
        if (it->index < sizes.size()) {
          sizes[it->index] = uncompressedSize;
          matched++;
        }
        ++it;
      }

      if (matched >= targetCount) {
        break;
      }
    } else {
      file.seekCur(nameLen);
    }

    file.seekCur(static_cast<int32_t>(extraLen) + commentLen);
  }

  return matched;
}

bool ZipFile::hasAnyDeflated() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;
  if (!loadZipDetails()) return false;

  file.seek(zipDetails.centralDirOffset);

  // Central directory file header, relative to the 4-byte signature:
  //   +4  version made by (2)      +6  version needed (2)   +8  flags (2)
  //   +10 COMPRESSION METHOD (2)   +12 mod time (2)         +14 mod date (2)
  //   +16 crc32 (4)                +20 compressed size (4)  +24 uncompressed size (4)
  //   +28 name length (2)          +30 extra length (2)     +32 comment length (2)
  //   +34 disk start (2)           +36 internal attrs (2)   +38 external attrs (4)
  //   +42 local header offset (4)  +46 name / extra / comment
  //
  // Walking this by hand is easy to get subtly wrong, and a desync here is silent:
  // it reads filename bytes as field lengths, seeks by a garbage amount, hits EOF and
  // reports "nothing deflated" for a book that is entirely deflated. EPUB requires the
  // "mimetype" entry to be first and STORED, so a broken walk desyncs on entry one for
  // essentially every book. Read the fixed header as one block and index it explicitly.
  constexpr size_t kFixedHeaderAfterSig = 42;  // +4 .. +46
  uint8_t hdr[kFixedHeaderAfterSig];

  while (true) {
    uint32_t sig = 0;
    // HalFile::read returns int on device and size_t in the host mock; normalise
    // through a signed local so a short read compares correctly in both builds.
    const int sigRead = static_cast<int>(file.read(&sig, sizeof(sig)));
    if (sigRead != static_cast<int>(sizeof(sig))) break;
    if (sig != ZIP_CENTRAL_DIR_SIG) break;  // reached the end-of-central-dir record

    const int hdrRead = static_cast<int>(file.read(hdr, sizeof(hdr)));
    if (hdrRead != static_cast<int>(sizeof(hdr))) break;

    // memcpy rather than a pointer cast: the ESP32-C3 faults on unaligned multi-byte
    // loads, and these fields are at arbitrary offsets within the buffer.
    uint16_t method = 0, nameLen = 0, extraLen = 0, commentLen = 0;
    memcpy(&method, hdr + 6, sizeof(method));  // +10 absolute
    if (method == ZIP_METHOD_DEFLATED) return true;

    memcpy(&nameLen, hdr + 24, sizeof(nameLen));        // +28 absolute
    memcpy(&extraLen, hdr + 26, sizeof(extraLen));      // +30 absolute
    memcpy(&commentLen, hdr + 28, sizeof(commentLen));  // +32 absolute

    if (!file.seekCur(static_cast<int32_t>(nameLen) + extraLen + commentLen)) break;
  }

  return false;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  file.seek(fileOffset);

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  // Reject absurd sizes a 380KB-RAM device can never hold. Also prevents the
  // uint32_t `inflatedDataSize + 1` from wrapping to 0 on a crafted entry
  // (uncompressedSize=0xFFFFFFFF), which would malloc(0) then read ~4GB OOB.
  constexpr uint32_t kMaxInflatedSize = 4u * 1024u * 1024u;
  if (inflatedDataSize > kMaxInflatedSize) {
    LOG_ERR("ZIP", "Entry too large: %u bytes", (unsigned)inflatedDataSize);
    return nullptr;
  }
  const size_t dataSize =
      trailingNullByte ? static_cast<size_t>(inflatedDataSize) + 1 : static_cast<size_t>(inflatedDataSize);
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(1024));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      free(data);
      return nullptr;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = 1024;

    if (!ctx.reader.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate reader");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    ctx.reader.setReadCallback(zipReadCallback);

    if (!ctx.reader.read(data, inflatedDataSize)) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    free(fileReadBuffer);

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize) {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  file.seek(fileOffset);
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      // HalFile::read() returns a negative int on error; as size_t it became a huge
      // length handed straight to out.write() (crossink bfe70a6ba).
      const int readResult = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (readResult <= 0) {
        LOG_ERR("ZIP", "Could not read more bytes: %d", readResult);
        free(buffer);
        return false;
      }
      const size_t dataRead = static_cast<size_t>(readResult);

      if (out.write(buffer, dataRead) != dataRead) {
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        free(buffer);
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    if (!ctx.reader.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate reader");
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    ctx.reader.setReadCallback(zipReadCallback);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      size_t produced;
      const InflateStatus status = ctx.reader.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          break;
        }
      }

      if (status == InflateStatus::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      if (status == InflateStatus::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }
      // InflateStatus::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // ctx.reader destructor frees the ring buffer
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}
