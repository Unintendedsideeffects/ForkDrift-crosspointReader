#pragma once

#include <HalStorage.h>

#include <cstdint>

namespace network {

struct DirScanEntry {
  const char* name;  // valid only during the callback
  bool isDirectory;
  uint32_t size;  // 0 for directories
};

// Walks `root` under SpiBusMutex, invoking cb for each entry that survives
// name extraction. Returns false only when the scan could not start.
bool forEachDirEntry(HalFile& root, void* ctx, void (*cb)(void* ctx, const DirScanEntry&));

}  // namespace network
