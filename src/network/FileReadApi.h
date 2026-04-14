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

// Validate and normalise rawPath for a file listing request.
// On error, the returned descriptor has a non-2xx status and a body with the
// error message. On success, normalizedPath contains the resolved SD path and
// body is empty.
FileListDescriptor resolveFileListPath(const String& rawPath);

// Build the complete JSON file listing in memory (used by host tests).
FileListDescriptor buildFileListJson(const String& rawPath, bool showHiddenFiles);

DownloadDescriptor resolveDownload(const String& rawPath);

}  // namespace network
