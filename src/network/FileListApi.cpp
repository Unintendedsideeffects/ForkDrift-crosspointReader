#include "network/FileListApi.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#define FILELIST_HAS_TASK_WDT 1
#else
#define FILELIST_HAS_TASK_WDT 0
#endif

#include "SpiBusMutex.h"
#include "util/PathUtils.h"

namespace network {

void scanDirectory(const char* path, bool showHiddenFiles, const std::function<void(const DirEntry&)>& callback) {
  FsFile root;
  {
    SpiBusMutex::Guard guard;
    root = Storage.open(path);
  }

  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    SpiBusMutex::Guard guard;
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  while (true) {
    DirEntry entry;
    bool shouldHide = false;

    {
      SpiBusMutex::Guard guard;
      FsFile file = root.openNextFile();
      if (!file) break;

      char name[500];
      if (!file.getName(name, sizeof(name))) {
        LOG_DBG("WEB", "Failed to get file name while scanning directory: %s", path);
        file.close();
        continue;
      }
      auto fileName = String(name);

      shouldHide = (!showHiddenFiles && fileName.startsWith(".")) ||
                   PathUtils::isProtectedWebComponent(fileName);

      if (!shouldHide) {
        entry.name = fileName;
        entry.isDirectory = file.isDirectory();
        if (entry.isDirectory) {
          entry.size = 0;
          entry.isEpub = false;
        } else {
          entry.size = file.size();
          entry.isEpub = FsHelpers::hasEpubExtension(fileName);
        }
      }
      file.close();
    }

    if (!shouldHide) {
      callback(entry);
    }

    // Yield outside the SPI mutex to allow other tasks to run and prevent WDT
    // resets during large directory scans.
    yield();
#if FILELIST_HAS_TASK_WDT
    esp_task_wdt_reset();
#endif
  }

  {
    SpiBusMutex::Guard guard;
    root.close();
  }
}

}  // namespace network
