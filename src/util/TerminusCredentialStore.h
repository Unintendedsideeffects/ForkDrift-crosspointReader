#pragma once

#include <string>

#include "util/TerminusApi.h"

/**
 * Singleton credential store for Terminus / TRMNL BYOS access.
 *
 * Credentials are persisted at /.crosspoint/terminus.json.
 * Initial setup: drop a JSON file at /terminus.json on the SD card root;
 * the device imports it into /.crosspoint/terminus.json on the next boot
 * and deletes the source file (consume-on-import).
 *
 * JSON schema:
 *   { "api_key": "...", "device_id": "...",
 *     "device_model": "xteink_x4",
 *     "base_url": "https://trmnl.com" }
 */
class TerminusCredentialStore {
 public:
  static TerminusCredentialStore& getInstance();

  // Load from SD card. Checks both the drop path and the persisted path.
  // Returns true if credentials were loaded (even from the drop file).
  bool load();
  bool save() const;
  void clear();

  bool hasCredentials() const;

  // Accessors (empty string when not configured)
  const std::string& apiKey() const { return apiKey_; }
  const std::string& deviceId() const { return deviceId_; }
  const std::string& deviceModel() const { return deviceModel_; }
  const std::string& baseUrl() const { return baseUrl_; }

  // Setters — call save() to persist.
  void setApiKey(const std::string& v) { apiKey_ = v; }
  void setDeviceId(const std::string& v) { deviceId_ = v; }
  void setDeviceModel(const std::string& v) { deviceModel_ = v; }
  void setBaseUrl(const std::string& v) { baseUrl_ = terminus_api::normalizeBaseUrl(v); }

  static constexpr const char* kStoredPath = "/.crosspoint/terminus.json";
  static constexpr const char* kDropPath = "/terminus.json";

 private:
  TerminusCredentialStore() = default;
  TerminusCredentialStore(const TerminusCredentialStore&) = delete;
  TerminusCredentialStore& operator=(const TerminusCredentialStore&) = delete;

  bool loadFromJson(const char* json);

  std::string apiKey_;
  std::string deviceId_;
  std::string deviceModel_ = "xteink_x4";
  std::string baseUrl_ = terminus_api::kDefaultBaseUrl;
};

#define TERMINUS_STORE TerminusCredentialStore::getInstance()
