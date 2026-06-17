#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

namespace network {

struct FileMutationResult {
  int statusCode;
  String body;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

using FileMutationCallback = std::function<void(const String&)>;

FileMutationResult createFolder(const String& rawParentPath, const String& folderName);
FileMutationResult renameFile(const String& rawItemPath, const String& rawRenameTarget, bool treatTargetAsName,
                              const FileMutationCallback& onPathChanged);
FileMutationResult moveFile(const String& rawItemPath, const String& rawDestPath,
                            const FileMutationCallback& onPathChanged);
FileMutationResult deletePaths(const std::vector<String>& rawPaths, const FileMutationCallback& onPathChanged);

}  // namespace network
