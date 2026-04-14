#include "network/FileMutationApi.h"

#include <HalStorage.h>

#include "SpiBusMutex.h"
#include "util/PathUtils.h"

namespace {

bool pathExists(const String& path) {
  SpiBusMutex::Guard guard;
  return Storage.exists(path.c_str());
}

bool openFileForMutation(const String& path, FsFile& file, bool& isDir) {
  SpiBusMutex::Guard guard;
  file = Storage.open(path.c_str());
  if (!file) {
    return false;
  }
  isDir = file.isDirectory();
  if (isDir) {
    file.close();
  }
  return true;
}

String decodeAndNormalizePath(const String& rawPath) {
  String path = PathUtils::urlDecode(rawPath);
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  return PathUtils::normalizePath(path);
}

}  // namespace

namespace network {

FileMutationResult createFolder(const String& rawParentPath, const String& folderName) {
  if (!PathUtils::isValidFilename(folderName)) {
    return {400, "Invalid folder name"};
  }
  if (PathUtils::isProtectedWebComponent(folderName)) {
    return {403, "Cannot create protected folders"};
  }

  String parentPath = rawParentPath.isEmpty() ? "/" : PathUtils::urlDecode(rawParentPath);
  if (!PathUtils::isValidSdPath(parentPath)) {
    return {400, "Invalid path"};
  }
  parentPath = PathUtils::normalizePath(parentPath);
  if (PathUtils::pathContainsProtectedItem(parentPath)) {
    return {403, "Cannot access protected items"};
  }

  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) {
    folderPath += "/";
  }
  folderPath += folderName;

  if (pathExists(folderPath)) {
    return {400, "Folder already exists"};
  }

  bool mkdirOk = false;
  {
    SpiBusMutex::Guard guard;
    mkdirOk = Storage.mkdir(folderPath.c_str());
  }
  if (!mkdirOk) {
    return {500, "Failed to create folder"};
  }

  return {200, "Folder created: " + folderName};
}

FileMutationResult renameFile(const String& rawItemPath, const String& rawRenameTarget, const bool treatTargetAsName,
                              const FileMutationCallback& onPathChanged) {
  String itemPath = decodeAndNormalizePath(rawItemPath);
  String renameTarget = rawRenameTarget;
  renameTarget.trim();

  if (!PathUtils::isValidSdPath(itemPath) || itemPath == "/") {
    return {400, "Invalid path"};
  }
  if (PathUtils::pathContainsProtectedItem(itemPath)) {
    return {403, "Cannot rename protected item"};
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }

  String newPath;
  String newName;
  if (treatTargetAsName) {
    newName = renameTarget;
    if (newName.isEmpty()) {
      return {400, "New name cannot be empty"};
    }
    if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
      return {400, "Invalid file name"};
    }
    if (newName.startsWith(".")) {
      return {403, "Cannot rename to hidden name"};
    }
    if (PathUtils::isProtectedWebComponent(newName)) {
      return {403, "Cannot rename to protected name"};
    }

    newPath = parentPath;
    if (!newPath.endsWith("/")) {
      newPath += "/";
    }
    newPath += newName;
  } else {
    newPath = decodeAndNormalizePath(renameTarget);
    if (!PathUtils::isValidSdPath(newPath) || newPath.isEmpty() || newPath == "/") {
      return {400, "Invalid path"};
    }
    if (PathUtils::pathContainsProtectedItem(newPath)) {
      return {403, "Cannot rename to protected path"};
    }

    newName = newPath.substring(newPath.lastIndexOf('/') + 1);
    if (newName.isEmpty()) {
      return {400, "Invalid file name"};
    }
    if (newName.startsWith(".")) {
      return {403, "Cannot rename to hidden name"};
    }
    if (PathUtils::isProtectedWebComponent(newName)) {
      return {403, "Cannot rename to protected name"};
    }
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    return {403, "Cannot rename protected item"};
  }
  if (newName == itemName) {
    return {200, "Name unchanged"};
  }
  if (!pathExists(itemPath)) {
    return {404, "Item not found"};
  }

  FsFile file;
  bool isDir = false;
  if (!openFileForMutation(itemPath, file, isDir)) {
    return {500, "Failed to open file"};
  }
  if (isDir) {
    return {400, "Only files can be renamed"};
  }
  if (pathExists(newPath)) {
    file.close();
    return {409, "Target already exists"};
  }

  if (onPathChanged) {
    onPathChanged(itemPath);
  }

  bool success = false;
  {
    SpiBusMutex::Guard guard;
    success = Storage.rename(itemPath.c_str(), newPath.c_str());
  }
  file.close();

  return success ? FileMutationResult{200, "Renamed successfully"}
                 : FileMutationResult{500, "Failed to rename file"};
}

FileMutationResult moveFile(const String& rawItemPath, const String& rawDestPath,
                            const FileMutationCallback& onPathChanged) {
  String itemPath = decodeAndNormalizePath(rawItemPath);
  String destPath = decodeAndNormalizePath(rawDestPath);

  if (!PathUtils::isValidSdPath(itemPath) || !PathUtils::isValidSdPath(destPath) || itemPath == "/") {
    return {400, "Invalid path"};
  }
  if (PathUtils::pathContainsProtectedItem(itemPath) || PathUtils::pathContainsProtectedItem(destPath)) {
    return {403, "Cannot move protected items"};
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    return {403, "Cannot move protected item"};
  }
  if (!pathExists(itemPath)) {
    return {404, "Item not found"};
  }

  FsFile file;
  bool isDir = false;
  if (!openFileForMutation(itemPath, file, isDir)) {
    return {500, "Failed to open file"};
  }
  if (isDir) {
    return {400, "Only files can be moved"};
  }
  if (!pathExists(destPath)) {
    file.close();
    return {404, "Destination not found"};
  }

  FsFile destDir;
  bool destIsDir = false;
  {
    SpiBusMutex::Guard guard;
    destDir = Storage.open(destPath.c_str());
    if (destDir) {
      destIsDir = destDir.isDirectory();
      destDir.close();
    }
  }
  if (!destIsDir) {
    file.close();
    return {400, "Destination is not a folder"};
  }

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    return {200, "Already in destination"};
  }
  if (pathExists(newPath)) {
    file.close();
    return {409, "Target already exists"};
  }

  if (onPathChanged) {
    onPathChanged(itemPath);
  }

  bool success = false;
  {
    SpiBusMutex::Guard guard;
    success = Storage.rename(itemPath.c_str(), newPath.c_str());
  }
  file.close();

  return success ? FileMutationResult{200, "Moved successfully"}
                 : FileMutationResult{500, "Failed to move file"};
}

FileMutationResult deletePaths(const std::vector<String>& rawPaths, const FileMutationCallback& onPathChanged) {
  if (rawPaths.empty()) {
    return {400, "No paths provided"};
  }

  bool allSuccess = true;
  String failedItems;

  for (String itemPath : rawPaths) {
    itemPath = decodeAndNormalizePath(itemPath);

    if (!PathUtils::isValidSdPath(itemPath)) {
      failedItems += itemPath + " (invalid path); ";
      allSuccess = false;
      continue;
    }
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }
    if (PathUtils::pathContainsProtectedItem(itemPath)) {
      failedItems += itemPath + " (protected path); ";
      allSuccess = false;
      continue;
    }
    if (!pathExists(itemPath)) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    bool success = false;
    bool folderNotEmpty = false;
    {
      SpiBusMutex::Guard guard;
      FsFile file = Storage.open(itemPath.c_str());
      if (file && file.isDirectory()) {
        FsFile entry = file.openNextFile();
        if (entry) {
          entry.close();
          folderNotEmpty = true;
        }
        file.close();
        if (!folderNotEmpty) {
          success = Storage.rmdir(itemPath.c_str());
        }
      } else {
        if (file) {
          file.close();
        }
        success = Storage.remove(itemPath.c_str());
      }
    }

    if (folderNotEmpty) {
      failedItems += itemPath + " (folder not empty); ";
      allSuccess = false;
      continue;
    }
    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
      continue;
    }

    if (onPathChanged) {
      onPathChanged(itemPath);
    }
  }

  if (!allSuccess) {
    return {500, "Failed to delete some items: " + failedItems};
  }
  if (rawPaths.size() == 1) {
    return {200, "Deleted successfully"};
  }
  return {200, "All items deleted successfully"};
}

}  // namespace network
