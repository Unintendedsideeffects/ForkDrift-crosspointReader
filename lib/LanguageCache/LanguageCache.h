#pragma once

#include <Memory.h>

#include <cstddef>
#include <cstdint>

namespace language_cache {
constexpr size_t SLOT_SIZE = 65536;
constexpr size_t HEADER_SIZE = 256;
constexpr size_t OWNER_SIZE = 16;
constexpr size_t CODE_SIZE = 32;
constexpr size_t NAME_SIZE = 96;
constexpr size_t MAX_KEYS = 2048;
constexpr uint16_t MISSING = 0xffff;
constexpr size_t MAX_SOURCE_SIZE = 256 * 1024;
constexpr char DIRECTORY[] = "/.crosspoint/languages";

struct Metadata {
  char code[CODE_SIZE] = {};
  char name[NAME_SIZE] = {};
  char keyboard[CODE_SIZE] = {};
  bool rtl = false;
};

struct Key {
  uint64_t hash;
  uint64_t englishHash;
  const char* name;
  uint16_t id;
  bool formatted;
};

// All callbacks run on cold paths; no ownership is transferred.
struct Schema {
  size_t count;
  Key (*at)(size_t);
  const char* (*english)(uint16_t);
};
struct Input {
  void* context;
  int (*read)(void*, void*, size_t);  // zero = EOF, negative = error
};
// The caller supplies firmware-owned storage. Installation may replace existing
// data in its final two slots; the rest of the region is never modified.
struct Flash {
  void* context;
  size_t size;
  bool (*read)(void*, size_t, void*, size_t);
  bool (*write)(void*, size_t, const void*, size_t);
  bool (*erase)(void*, size_t, size_t);
};

enum class Result : uint8_t { Ok, Io, Memory, Invalid, TooLarge, Duplicate, MetadataMissing, StorageUnavailable };
const char* resultName(Result result);
uint64_t hash(const char* text);
bool validUtf8(const char* text, size_t length);
bool validCode(const char* code);
bool compatibleFormat(const char* english, const char* translated);

// Reuse a single bounded workspace while enumerating a language directory.
class Inspector {
 public:
  Inspector();
  ~Inspector();
  bool available() const { return bool(workspace_); }
  Result inspect(Input input, Metadata& metadata);

 private:
  struct Impl;
  HeapObject<Impl> workspace_;
};

// Inspect metadata without reading beyond the first string. Metadata must precede strings.
Result inspect(Input input, Metadata& metadata);

struct Installed {
  Metadata metadata;
  uint64_t generation = 0;
  uint32_t checksum = 0;
  int slot = -1;
};

// The caller pins the selected slot until restart. Installation never changes it.
Result install(Input input, const Schema& schema, const Flash& flash, int pinnedSlot, Installed& installed);

// Fully validates a committed mapped slot. Offsets are relative to the slot base.
// Pass nullptr for offsets to inspect a candidate without changing the active index.
bool open(const uint8_t* bytes, size_t length, const Schema& schema, Installed& installed, uint16_t* offsets = nullptr);
size_t slotOffset(size_t partitionSize, int slot);
}  // namespace language_cache
