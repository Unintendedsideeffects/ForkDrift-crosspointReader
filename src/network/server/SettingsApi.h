#pragma once

#include <Arduino.h>

namespace network {

struct SettingsApplyResult {
  int statusCode;
  String contentType;
  String body;
  int appliedCount;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Sink for streamSettingsListJson: receives each JSON fragment in order — the
// opening "[", each setting object (comma-prefixed as needed), and the closing
// "]". Lets the caller forward fragments straight into a chunked HTTP response
// without ever buffering the whole array.
using SettingChunkSink = void (*)(void* ctx, const char* chunk);

// Streams the editable-settings JSON array to `sink` one fragment at a time.
// Builds no std::vector<SettingInfo> and no cumulative String: settings are
// generated and serialized one at a time, bounding peak heap to a single entry.
// SD / font-registry access is confined to a short SpiBusMutex-guarded prelude.
void streamSettingsListJson(SettingChunkSink sink, void* ctx);
SettingsApplyResult applySettingsJson(const String& body);

}  // namespace network
