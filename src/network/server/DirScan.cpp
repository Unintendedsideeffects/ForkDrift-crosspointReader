#include "network/server/DirScan.h"

#include "SpiBusMutex.h"

namespace network {

bool forEachDirEntry(HalFile& root, void* ctx, void (*cb)(void* ctx, const DirScanEntry&)) {
  if (!root || !root.isDirectory() || cb == nullptr) {
    if (root) {
      SpiBusMutex::Guard guard;
      root.close();
    }
    return false;
  }

  while (true) {
    char name[500];
    bool done = false;
    bool extracted = false;
    bool isDirectory = false;
    uint32_t size = 0;

    {
      SpiBusMutex::Guard guard;
      HalFile file = root.openNextFile();
      if (!file) {
        done = true;
      } else if (!file.getName(name, sizeof(name))) {
        file.close();
      } else {
        isDirectory = file.isDirectory();
        if (!isDirectory) {
          size = static_cast<uint32_t>(file.size());
        }
        file.close();
        extracted = true;
      }
    }

    if (done) break;
    if (extracted) {
      const DirScanEntry entry{name, isDirectory, size};
      cb(ctx, entry);
    }
  }

  {
    SpiBusMutex::Guard guard;
    root.close();
  }
  return true;
}

}  // namespace network
