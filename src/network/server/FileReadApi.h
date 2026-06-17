#pragma once

#include <Arduino.h>
#include <WebServer.h>

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

// Stream the JSON file listing directly to the client to avoid building a
// large in-memory response for big directories.
bool streamFileListJson(WebServer& server, const String& rawPath, bool showHiddenFiles,
                        String* normalizedPath = nullptr, size_t* entryCount = nullptr);

DownloadDescriptor resolveDownload(const String& rawPath);

}  // namespace network
