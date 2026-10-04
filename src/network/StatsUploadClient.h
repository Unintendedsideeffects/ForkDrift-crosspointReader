#pragma once

#include <memory>

// App-facing transport for the CrossPoint stats snapshot API.
class StatsUploadClient {
 public:
  enum class Result { Ok, Network, Auth, Unsupported, InvalidResponse, LowMemory, Server, Skipped };
  StatsUploadClient();
  ~StatsUploadClient();
  Result put(const char* endpoint, const char* payload, bool book, bool daily = false);
  static const char* errorString(Result result);
  static bool failed(Result result) { return result != Result::Ok && result != Result::Skipped; }
  static const char* resultString(Result result);
  static bool deviceId(char* out, size_t capacity);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
