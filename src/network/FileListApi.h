#pragma once

#include <Arduino.h>

#include <cstddef>
#include <functional>

namespace network {

struct DirEntry {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

// Scan a directory on the SD card and invoke callback for each visible entry.
// Entries starting with '.' are excluded when showHiddenFiles is false.
// Protected web-server components (e.g. .crosspoint) are always excluded.
void scanDirectory(const char* path, bool showHiddenFiles, const std::function<void(const DirEntry&)>& callback);

}  // namespace network
