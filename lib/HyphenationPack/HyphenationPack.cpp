#include "HyphenationPack.h"

#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace hyphenation_pack {
namespace {
constexpr uint32_t COMMITTED = 0x50485950;
constexpr size_t ENTRY_OFFSET = 32;
constexpr size_t ENTRY_SIZE = 20;
constexpr size_t BUFFER_SIZE = 1024;
static_assert(ENTRY_OFFSET + MAX_PACKS * ENTRY_SIZE <= HEADER_SIZE);

// One 1280-byte cold-path workspace; too large for the C3 task stack and
// unnecessary as permanent RAM. Reused for all checks/copies in an operation.
struct Workspace {
  uint8_t header[HEADER_SIZE];
  uint8_t buffer[BUFFER_SIZE];
};
uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void put32(uint8_t* p, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
size_t align4(size_t n) { return (n + 3) & ~size_t{3}; }
Entry decode(const uint8_t* p) {
  Entry entry;
  entry.code[0] = static_cast<char>(p[0]);
  entry.code[1] = static_cast<char>(p[1]);
  entry.minPrefix = p[2];
  entry.minSuffix = p[3];
  entry.offset = u32(p + 4);
  entry.size = u32(p + 8);
  entry.rootOffset = u32(p + 12);
  entry.checksum = u32(p + 16);
  return entry;
}
void encode(uint8_t* p, const Entry& entry) {
  p[0] = entry.code[0];
  p[1] = entry.code[1];
  p[2] = entry.minPrefix;
  p[3] = entry.minSuffix;
  put32(p + 4, entry.offset);
  put32(p + 8, entry.size);
  put32(p + 12, entry.rootOffset);
  put32(p + 16, entry.checksum);
}
uint32_t headerCrc(const uint8_t* header) {
  uint32_t crc = crc32(header, 20);
  const uint8_t zeros[4] = {};
  crc = crc32(zeros, sizeof(zeros), crc);
  // The commit marker is programmed last and excluded from the checksum.
  return crc32(header + 28, HEADER_SIZE - 28, crc);
}
bool validHeader(const uint8_t* header, size_t bankSize, Supported supported) {
  if (std::memcmp(header, "CIHP\1\0\0\0", 8) || u32(header + 24) != COMMITTED || !u32(header + 8) ||
      u32(header + 12) > MAX_PACKS || u32(header + 28) || u32(header + 20) != headerCrc(header))
    return false;
  size_t end = SECTOR_SIZE;
  for (size_t i = 0; i < u32(header + 12); ++i) {
    const Entry entry = decode(header + ENTRY_OFFSET + i * ENTRY_SIZE);
    if (!supported(entry.code, entry.minPrefix, entry.minSuffix) || entry.offset != end || !entry.size ||
        entry.rootOffset >= entry.size || entry.offset > bankSize || entry.size > bankSize - entry.offset)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (!std::memcmp(entry.code, header + ENTRY_OFFSET + j * ENTRY_SIZE, 2)) return false;
    end = align4(entry.offset + entry.size);
  }
  return end == u32(header + 16) && end <= bankSize;
}
bool checkPayload(const Flash& flash, size_t offset, size_t size, uint32_t expected, uint8_t* buffer) {
  uint32_t crc = 0;
  for (size_t copied = 0; copied < size;) {
    const size_t n = std::min(BUFFER_SIZE, size - copied);
    if (!flash.read(flash.context, offset + copied, buffer, n)) return false;
    crc = crc32(buffer, n, crc);
    copied += n;
    if (flash.yield && copied % (16 * BUFFER_SIZE) == 0) flash.yield();
  }
  return crc == expected;
}
bool validPayloads(const Flash& flash, size_t bankOffset, const uint8_t* header, uint8_t* buffer) {
  for (size_t i = 0; i < u32(header + 12); ++i) {
    const Entry entry = decode(header + ENTRY_OFFSET + i * ENTRY_SIZE);
    if (!checkPayload(flash, bankOffset + entry.offset, entry.size, entry.checksum, buffer)) return false;
  }
  return true;
}
}  // namespace

uint32_t crc32(const void* bytes, size_t length, uint32_t previous) {
  auto crc = ~previous;
  const auto* data = static_cast<const uint8_t*>(bytes);
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (size_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
uint32_t identity(const Entry& entry) {
  uint8_t bytes[ENTRY_SIZE];
  encode(bytes, entry);
  put32(bytes + 4, 0);  // A bank relocation does not change hyphenation.
  const uint32_t result = crc32(bytes, sizeof(bytes));
  return result ? result : 1;  // zero identifies an absent/disabled pattern set
}
const char* resultName(Result result) {
  switch (result) {
    case Result::Ok:
      return "installed; restart required";
    case Result::Unchanged:
      return "unchanged";
    case Result::Invalid:
      return "invalid pack or checksum";
    case Result::Unsupported:
      return "unsupported language or word rules";
    case Result::Io:
      return "SD or flash I/O error";
    case Result::Memory:
      return "out of memory";
    case Result::NoSpace:
      return "hyphenation storage full";
    case Result::Unavailable:
      return "hyphenation storage unavailable";
    case Result::RestartRequired:
      return "restart before another update";
  }
  return "unknown error";
}
Result Store::begin(Flash flash, Supported supported) {
  if (ready_) return Result::Unchanged;
  if (!supported || !flash.read || !flash.write || !flash.erase || flash.size < 2 * 65536 || flash.size % (2 * 65536) ||
      flash.size > 16 * 1024 * 1024)
    return Result::Unavailable;
  auto work = makeUniqueNoThrow<Workspace>();
  if (!work) return Result::Memory;
  flash_ = flash;
  supported_ = supported;
  bankSize_ = flash.size / 2;
  bank_ = -1;  // A previous attempt may have failed after reading only one bank.
  for (int bank = 0; bank < 2; ++bank) {
    const size_t base = static_cast<size_t>(bank) * bankSize_;
    if (!flash.read(flash.context, base, work->header, HEADER_SIZE)) return Result::Io;
    if (!validHeader(work->header, bankSize_, supported) || !validPayloads(flash, base, work->header, work->buffer))
      continue;
    if (bank_ < 0 || u32(work->header + 8) > u32(header_ + 8)) {
      std::memcpy(header_, work->header, HEADER_SIZE);
      bank_ = bank;
    }
  }
  ready_ = true;
  return Result::Ok;
}
size_t Store::count() const { return bank_ < 0 ? 0 : u32(header_ + 12); }
size_t Store::usedBytes() const { return bank_ < 0 ? 0 : u32(header_ + 16); }
bool Store::find(const char* code, Entry& entry) const {
  if (!code || std::strlen(code) != 2) return false;
  for (size_t i = 0; i < count(); ++i) {
    const Entry candidate = decode(header_ + ENTRY_OFFSET + i * ENTRY_SIZE);
    if (!std::strcmp(candidate.code, code)) {
      entry = candidate;
      return true;
    }
  }
  return false;
}
Result Store::install(Input input) {
  if (!ready_) return Result::Unavailable;
  if (pending_) return Result::RestartRequired;
  uint8_t header[PACK_HEADER_SIZE];
  if (!input.read || input.size < sizeof(header)) return Result::Invalid;
  if (!input.read(input.context, 0, header, sizeof(header))) return Result::Io;
  if (std::memcmp(header, "CPHY", 4) || header[4] != 1 || header[9] || header[10] || header[11]) return Result::Invalid;
  Entry entry;
  entry.code[0] = header[5];
  entry.code[1] = header[6];
  entry.minPrefix = header[7];
  entry.minSuffix = header[8];
  entry.rootOffset = u32(header + 12);
  entry.size = u32(header + 16);
  entry.checksum = u32(header + 20);
  if (!entry.size || entry.rootOffset >= entry.size || entry.size != input.size - sizeof(header))
    return Result::Invalid;
  if (!supported_(entry.code, entry.minPrefix, entry.minSuffix)) return Result::Unsupported;
  return rewrite(entry.code, &input, &entry);
}
Result Store::remove(const char* code) {
  if (!ready_) return Result::Unavailable;
  if (pending_) return Result::RestartRequired;
  Entry existing;
  if (!find(code, existing)) return Result::Unchanged;
  return rewrite(code, nullptr, nullptr);
}
Result Store::rewrite(const char* code, const Input* input, const Entry* replacement) {
  if (bank_ >= 0 && u32(header_ + 8) == std::numeric_limits<uint32_t>::max()) return Result::Unavailable;
  auto work = makeUniqueNoThrow<Workspace>();
  if (!work) return Result::Memory;
  std::memset(work->header, 0, HEADER_SIZE);
  std::memcpy(work->header, "CIHP\1\0\0\0", 8);
  put32(work->header + 8, bank_ < 0 ? 1 : u32(header_ + 8) + 1);
  put32(work->header + 24, 0xffffffffu);
  size_t next = SECTOR_SIZE;
  size_t entries = 0;
  for (size_t i = 0; i < count(); ++i) {
    Entry entry = decode(header_ + ENTRY_OFFSET + i * ENTRY_SIZE);
    if (!std::strcmp(entry.code, code)) continue;
    entry.offset = next;
    encode(work->header + ENTRY_OFFSET + entries++ * ENTRY_SIZE, entry);
    next = align4(next + entry.size);
  }
  if (replacement) {
    if (entries == MAX_PACKS || next > bankSize_ || replacement->size > bankSize_ - next) return Result::NoSpace;
    Entry entry = *replacement;
    entry.offset = next;
    encode(work->header + ENTRY_OFFSET + entries++ * ENTRY_SIZE, entry);
    next = align4(next + entry.size);
    uint32_t crc = 0;
    for (size_t copied = 0; copied < entry.size;) {
      const size_t n = std::min(BUFFER_SIZE, entry.size - copied);
      if (!input->read(input->context, PACK_HEADER_SIZE + copied, work->buffer, n)) return Result::Io;
      crc = crc32(work->buffer, n, crc);
      copied += n;
      if (flash_.yield && copied % (16 * BUFFER_SIZE) == 0) flash_.yield();
    }
    if (crc != entry.checksum) return Result::Invalid;
    Entry existing;
    if (find(code, existing) && existing.size == entry.size && existing.rootOffset == entry.rootOffset &&
        existing.minPrefix == entry.minPrefix && existing.minSuffix == entry.minSuffix &&
        existing.checksum == entry.checksum)
      return Result::Unchanged;
  }
  put32(work->header + 12, entries);
  put32(work->header + 16, next);
  put32(work->header + 20, headerCrc(work->header));
  const size_t target = bank_ == 0 ? bankSize_ : 0;
  for (size_t offset = 0; offset < next; offset += SECTOR_SIZE) {
    if (!flash_.erase(flash_.context, target + offset, SECTOR_SIZE)) return Result::Io;
    if (flash_.yield) flash_.yield();
  }
  for (size_t i = 0; i < entries; ++i) {
    const Entry entry = decode(work->header + ENTRY_OFFSET + i * ENTRY_SIZE);
    const bool added = replacement && !std::strcmp(entry.code, code);
    Entry source;
    if (!added && !find(entry.code, source)) return Result::Invalid;
    for (size_t copied = 0; copied < entry.size;) {
      const size_t n = std::min(BUFFER_SIZE, entry.size - copied);
      const bool read = added ? input->read(input->context, PACK_HEADER_SIZE + copied, work->buffer, n)
                              : flash_.read(flash_.context, bankOffset() + source.offset + copied, work->buffer, n);
      if (!read || !flash_.write(flash_.context, target + entry.offset + copied, work->buffer, n)) return Result::Io;
      copied += n;
      if (flash_.yield) flash_.yield();
    }
  }
  // Verify the actual flash bytes, including copied old packs, before publishing.
  if (!validPayloads(flash_, target, work->header, work->buffer) ||
      !flash_.write(flash_.context, target, work->header, HEADER_SIZE) ||
      !flash_.read(flash_.context, target, work->buffer, HEADER_SIZE) ||
      std::memcmp(work->header, work->buffer, HEADER_SIZE))
    return Result::Io;
  uint8_t marker[4];
  put32(marker, COMMITTED);
  if (!flash_.write(flash_.context, target + 24, marker, sizeof(marker)) ||
      !flash_.read(flash_.context, target + 24, work->buffer, sizeof(marker)) ||
      std::memcmp(marker, work->buffer, sizeof(marker)))
    return Result::Io;
  pending_ = true;
  return Result::Ok;
}
}  // namespace hyphenation_pack
