#include "HyphenationPackStore.h"

#include <Arduino.h>
#include <HalFlashPartition.h>
#include <HalStorage.h>
#include <LanguageCache.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace HyphenationPackStore {
namespace {
HalFlashPartition flash;
HalFlashPartition::Mapping mapping;
hyphenation_pack::Store store;
bool initialized = false;

// Same partition, separate ownership: the last two slots belong to UI I18n.
size_t capacity() {
  return flash.size() < 2 * language_cache::SLOT_SIZE ? 0 : language_cache::slotOffset(flash.size(), 0);
}
bool contains(size_t offset, size_t size) { return offset <= capacity() && size <= capacity() - offset; }
bool readFlash(void*, size_t offset, void* bytes, size_t size) {
  return contains(offset, size) && flash.read(offset, bytes, size);
}
bool writeFlash(void*, size_t offset, const void* bytes, size_t size) {
  return contains(offset, size) && flash.write(offset, bytes, size);
}
bool eraseFlash(void*, size_t offset, size_t size) { return contains(offset, size) && flash.erase(offset, size); }
bool supported(const char* code, uint8_t prefix, uint8_t suffix) {
  const auto* language = findLanguageEntry(code);
  return language && !language->hyphenator && prefix == 2 && suffix == 2;
}
bool initialize(bool create) {
  if (initialized) return true;
  if (!flash.begin(create)) return false;
  const auto result =
      store.begin({nullptr, capacity(), readFlash, writeFlash, eraseFlash, [] { delay(1); }}, supported);
  if (result != hyphenation_pack::Result::Ok && result != hyphenation_pack::Result::Unchanged) {
    LOG_ERR("HYPH", "%s", hyphenation_pack::resultName(result));
    return false;
  }
  if (store.count() && !flash.map(store.bankOffset(), store.usedBytes(), mapping)) {
    LOG_ERR("HYPH", "Cannot map installed patterns");
    return false;
  }
  initialized = true;
  LOG_INF("HYPH", "Mapped %u packs (%u bytes)", unsigned(store.count()), unsigned(mapping.size));
  return true;
}
bool sourcePath(const char* code, char (&path)[64]) {
  if (!supported(code, 2, 2)) return false;
  std::snprintf(path, sizeof(path), "%s/hyph-%s.cphyph", DIRECTORY, code);
  return true;
}
bool readInput(void* context, size_t offset, void* bytes, size_t size) {
  auto* file = static_cast<FsFile*>(context);
  return file->seek(offset) && file->read(bytes, size) == static_cast<int>(size);
}
}  // namespace

bool begin() {
  setExternalHyphenationLookup(lookup);
  return initialize(false);
}
bool lookup(const char* code, ExternalHyphenationPatterns& out) {
  hyphenation_pack::Entry entry;
  if (!initialized || !mapping.data || !store.find(code, entry)) return false;
  out.patterns = {entry.rootOffset, mapping.data + entry.offset, entry.size};
  out.identity = hyphenation_pack::identity(entry);
  return true;
}
bool isInstalled(const char* code) {
  ExternalHyphenationPatterns found{};
  return lookup(code, found);
}
bool hasSource(const char* code) {
  char path[64];
  return sourcePath(code, path) && Storage.exists(path);
}
hyphenation_pack::Result install(const char* code) {
  using hyphenation_pack::Result;
  char path[64];
  if (!sourcePath(code, path)) return Result::Unsupported;
  if (!initialize(true)) return Result::Unavailable;
  FsFile file;
  if (!Storage.openFileForRead("HYPH", path, file)) return Result::Io;
  // Require the header identity to match the selected filename. Never import
  // another language merely because its file was renamed on the SD card.
  char headerCode[2];
  if (!file.seek(5) || file.read(headerCode, sizeof(headerCode)) != sizeof(headerCode) ||
      std::memcmp(headerCode, code, sizeof(headerCode))) {
    file.close();
    LOG_ERR("HYPH", "Pack language does not match %s", path);
    return Result::Invalid;
  }
  const auto result = store.install({&file, file.size(), readInput});
  file.close();
  if (result != Result::Ok && result != Result::Unchanged)
    LOG_ERR("HYPH", "%s: %s", code, hyphenation_pack::resultName(result));
  return result;
}
hyphenation_pack::Result remove(const char* code) {
  const auto result = store.remove(code);
  if (result != hyphenation_pack::Result::Ok && result != hyphenation_pack::Result::Unchanged)
    LOG_ERR("HYPH", "%s: %s", code, hyphenation_pack::resultName(result));
  return result;
}
}  // namespace HyphenationPackStore
