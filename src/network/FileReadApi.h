#pragma once

#include <Arduino.h>

namespace network {

struct DownloadDescriptor {
  int statusCode;
  String contentType;
  String body;
  String normalizedPath;
  String filename;
  size_t fileSize = 0;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

struct FileListDescriptor {
  int statusCode;
  String contentType;
  String body;
  String normalizedPath;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

FileListDescriptor buildFileListJson(const String& rawPath, bool showHiddenFiles);
DownloadDescriptor resolveDownload(const String& rawPath);

}  // namespace network
