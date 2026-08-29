#pragma once

#include <cstdint>
#include <string>

// Disposable STORED ZIP beside firmware cache data. The original EPUB stays
// the source of truth. First slice: write a validated method-0 archive so a
// later convert step can land without a ZIP-writer pipeline. Cap is 16 entries.

struct StoredShadowEntry {
  const char* name = nullptr;
  const uint8_t* data = nullptr;
  uint32_t size = 0;
};

namespace stored_shadow {

std::string pathBesideCache(const std::string& cachePath);

bool writeArchive(const char* path, const StoredShadowEntry* entries, int count);

}  // namespace stored_shadow
