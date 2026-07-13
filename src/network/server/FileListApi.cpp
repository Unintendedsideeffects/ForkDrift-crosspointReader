#include "network/server/FileListApi.h"

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
#include "network/server/DirScan.h"
#include "util/PathUtils.h"

namespace network {

namespace {

struct FileListScanContext {
  bool showHiddenFiles;
  const std::function<void(const DirEntry&)>* callback;
};

void handleFileListEntry(void* rawContext, const DirScanEntry& scanned) {
  auto& context = *static_cast<FileListScanContext*>(rawContext);
  const String fileName(scanned.name);
  const bool isDotFile = fileName.startsWith(".");
  // Dotfiles are hidden when showHiddenFiles is false; they remain visible
  // when the user explicitly opts in. Named protected components (e.g.
  // "System Volume Information") are always hidden regardless.
  const bool shouldHide =
      (!context.showHiddenFiles && isDotFile) || (!isDotFile && PathUtils::isProtectedWebComponent(fileName));

  if (!shouldHide) {
    DirEntry entry;
    entry.name = fileName;
    entry.isDirectory = scanned.isDirectory;
    if (entry.isDirectory) {
      entry.size = 0;
      entry.isEpub = false;
    } else {
      entry.size = scanned.size;
      entry.isEpub = FsHelpers::hasEpubExtension(fileName);
    }
    (*context.callback)(entry);
  }

  // Yield outside the SPI mutex to allow other tasks to run and prevent WDT
  // resets during large directory scans.
#if defined(ARDUINO)
  yield();
#if FILELIST_HAS_TASK_WDT
  esp_task_wdt_reset();
#endif
#endif
}

}  // namespace

void scanDirectory(const char* path, const bool showHiddenFiles, const std::function<void(const DirEntry&)>& callback) {
  HalFile root;
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

  FileListScanContext context{showHiddenFiles, &callback};
  forEachDirEntry(root, &context, handleFileListEntry);
}

}  // namespace network
