#pragma once

#include <KOReaderSyncClient.h>

#include <string>

#include "StatsUploadClient.h"

// Foreground helpers shared by reader sync and the explicit folder upload.
namespace ReadingSyncUpload {
// Only saved, mappable EPUB positions qualify. Never invent progress for an unread book.
bool prepareProgress(const std::string& path, KOReaderProgress& progress);
struct ExtrasResult {
  StatsUploadClient::Result stats = StatsUploadClient::Result::Skipped;
  StatsUploadClient::Result clippings = StatsUploadClient::Result::Skipped;
  bool success() const { return !StatsUploadClient::failed(stats) && !StatsUploadClient::failed(clippings); }
};
StatsUploadClient::Result globalStats();
// Reuse the explicit stats activity's existing buffer/client rather than allocating a second pair.
StatsUploadClient::Result globalStats(StatsUploadClient& client, char* payload, size_t capacity, const char* deviceId);
StatsUploadClient::Result stats(const std::string& path, const std::string& document);
ExtrasResult extras(const std::string& path, const std::string& document);
}  // namespace ReadingSyncUpload
