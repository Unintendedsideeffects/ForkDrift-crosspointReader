#pragma once

#include <KOReaderSyncClient.h>

#include <cstddef>
#include <cstdint>
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
// Probe an unknown server once when an automatic include choice depends on it.
void refreshServerSupport();
StatsUploadClient::Result globalStats();
// Reuse the explicit stats activity's existing buffer/client rather than allocating a second pair.
StatsUploadClient::Result globalStats(StatsUploadClient& client, char* payload, size_t capacity, const char* deviceId);
StatsUploadClient::Result stats(const std::string& path, const std::string& document);
ExtrasResult extras(const std::string& path, const std::string& document);
// Shared result wording for every sync screen: "Not supported" on a progress-only
// server, "OFF" when the user turned that data off, otherwise the upload result.
const char* statusLabel(StatsUploadClient::Result result, bool clippings);
// Bulk variant: "3" or "3 (1 failed)", with the same OFF / Not supported states.
const char* countLabel(char* buffer, size_t capacity, uint32_t ok, uint32_t failed, bool clippings);
}  // namespace ReadingSyncUpload
