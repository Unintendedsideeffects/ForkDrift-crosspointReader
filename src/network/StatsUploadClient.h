#pragma once

#include <memory>

// App-facing transport for the CrossPoint stats snapshot API.
class StatsUploadClient {
 public:
  enum class Result { Ok, Network, Auth, Unsupported, InvalidResponse, LowMemory, Server, Skipped };
  StatsUploadClient();
  ~StatsUploadClient();
  Result put(const char* endpoint, const char* payload, bool book, bool daily = false);
  // Read-only capability check against the stats summary endpoint. Records the
  // answer in the credential store; Unsupported means a KOSync-only server.
  // A non-JSON 200 (captive portal, proxy page) is only persisted when
  // `trustHtml` is set, i.e. right after the server accepted our credentials.
  Result probe(bool trustHtml = false);
  static const char* errorString(Result result);
  // A server without the extension API is not an upload failure: progress-only
  // servers simply have nothing to receive stats or clippings.
  static bool failed(Result result) {
    return result != Result::Ok && result != Result::Skipped && result != Result::Unsupported;
  }
  static const char* resultString(Result result);
  static bool deviceId(char* out, size_t capacity);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
