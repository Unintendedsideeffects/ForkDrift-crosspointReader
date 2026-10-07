#include "StatsUploadClient.h"

#include <ArduinoJson.h>
#include <I18n.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#ifndef SIMULATOR
#include <esp_mac.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

namespace {
// Only definitive answers update the persisted capability; transient failures keep it.
void recordMissingApi(const int status) {
  if (status == 404 || status == 405) KOREADER_STORE.setServerSupport(SyncServerSupport::UNSUPPORTED);
}
}  // namespace

struct StatsUploadClient::Impl {
  freeink::SecureHttpClient http;
  char response[256]{};
};

// Keep the client and bounded reply buffer off the small main-task stack.
StatsUploadClient::StatsUploadClient() : impl(makeUniqueNoThrow<Impl>()) {}
StatsUploadClient::~StatsUploadClient() = default;

bool StatsUploadClient::deviceId(char* out, const size_t capacity) {
#ifdef SIMULATOR
  // Deliberately separate from hardware identities; never overwrite a reader.
  const int n = snprintf(out, capacity, "crossink-simulator");
#else
  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    LOG_ERR("StatsSync", "Cannot read device identity");
    return false;
  }
  const int n =
      snprintf(out, capacity, "crossink-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
  return n > 0 && static_cast<size_t>(n) < capacity;
}

StatsUploadClient::Result StatsUploadClient::put(const char* endpoint, const char* payload, const bool book,
                                                 const bool daily) {
  if (!impl) {
    LOG_ERR("StatsSync", "Cannot allocate HTTP client");
    return Result::LowMemory;
  }
  auto& http = impl->http;
  // Retain a successful connection while headroom permits. If TLS is holding
  // needed heap, release it before applying the normal handshake floors.
  if (ESP.getFreeHeap() < 35000 || ESP.getMaxAllocHeap() < 20000) http.end();
  if (ESP.getFreeHeap() < 35000 || ESP.getMaxAllocHeap() < 20000) {
    LOG_ERR("StatsSync", "Insufficient memory for upload");
    return Result::LowMemory;
  }
  // Do not follow redirects or send Basic credentials: this API only needs
  // the x-auth headers. Reuse the existing KOReader TLS policy.
  http.setInsecure();
  http.setTimeout(15000);
  if (!http.begin(KOREADER_STORE.getBaseUrl() + endpoint)) {
    LOG_ERR("StatsSync", "Invalid server URL");
    http.end();
    return Result::Network;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password());
  size_t size = 0;
  bool complete = true;
#ifdef SIMULATOR
  const int status = http.sendRequest("PUT", std::string(payload));
  const auto body = http.getString();
  size = body.length();
  complete = size < sizeof(impl->response);
  if (complete) memcpy(impl->response, body.c_str(), size);
#else
  const int status = http.sendRequest("PUT", reinterpret_cast<const uint8_t*>(payload), strlen(payload),
                                      [&](const uint8_t* data, size_t length) {
                                        if (length >= sizeof(impl->response) - size) return false;
                                        memcpy(impl->response + size, data, length);
                                        size += length;
                                        return true;
                                      });
  complete = http.responseComplete();
#endif
  LOG_INF("StatsSync", "Upload response: HTTP %d", status);
  recordMissingApi(status);
  auto failure = [&](Result result) {
    http.end();
    return result;
  };
  if (status <= 0) return failure(Result::Network);
  if (status == 401) return failure(Result::Auth);
  if (status == 404 || status == 405) return failure(Result::Unsupported);
  if (status != 200) return failure(Result::Server);
  if (!complete) return failure(Result::InvalidResponse);
  impl->response[size] = '\0';
  JsonDocument reply;
  if (deserializeJson(reply, impl->response) || !reply["until"].is<uint64_t>() ||
      (book && (!reply["accepted"].is<unsigned>() || reply["accepted"].as<unsigned>() != 1)) ||
      (daily && (!reply["accepted_daily"].is<unsigned>() || reply["accepted_daily"].as<unsigned>() != 1))) {
    return failure(Result::InvalidResponse);
  }
  // Only a valid extension reply proves support; a catch-all 200 page does not.
  KOREADER_STORE.setServerSupport(SyncServerSupport::SUPPORTED);
  return Result::Ok;
}

StatsUploadClient::Result StatsUploadClient::probe(const bool trustHtml) {
  if (!impl) {
    LOG_ERR("StatsSync", "Cannot allocate HTTP client");
    return Result::LowMemory;
  }
  auto& http = impl->http;
  if (ESP.getFreeHeap() < 35000 || ESP.getMaxAllocHeap() < 20000) {
    LOG_ERR("StatsSync", "Insufficient memory for capability probe");
    return Result::LowMemory;
  }
  http.setInsecure();
  http.setTimeout(15000);
  if (!http.begin(KOREADER_STORE.getBaseUrl() + "/api/v1/stats/summary")) {
    LOG_ERR("StatsSync", "Invalid server URL");
    http.end();
    return Result::Network;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password());
  // A JSON object body separates the summary API from catch-all HTML pages that
  // some proxies return with 200; only its first non-space byte is kept.
  char first = '\0';
#ifdef SIMULATOR
  const int status = http.GET();
  for (const char c : std::string(http.getString().c_str())) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      first = c;
      break;
    }
  }
#else
  const int status = http.GET([&first](const uint8_t* data, size_t length) {
    for (size_t i = 0; first == '\0' && i < length; ++i) {
      if (data[i] != ' ' && data[i] != '\n' && data[i] != '\r' && data[i] != '\t') first = static_cast<char>(data[i]);
    }
    return true;  // Drain the rest of the summary without storing it.
  });
#endif
  http.end();
  LOG_INF("StatsSync", "Capability probe: HTTP %d", status);
  recordMissingApi(status);
  if (status <= 0) return Result::Network;
  if (status == 401) return Result::Auth;
  if (status == 404 || status == 405) return Result::Unsupported;
  if (status != 200) return Result::Server;
  if (first != '{') {
    LOG_INF("StatsSync", "Capability probe reply is not the stats API");
    // Mid-sync this may be a captive portal; leave it unknown until Authenticate.
    if (!trustHtml) return Result::InvalidResponse;
    KOREADER_STORE.setServerSupport(SyncServerSupport::UNSUPPORTED);
    return Result::Unsupported;
  }
  KOREADER_STORE.setServerSupport(SyncServerSupport::SUPPORTED);
  return Result::Ok;
}

const char* StatsUploadClient::resultString(Result result) {
  if (result == Result::Ok) return tr(STR_DONE);
  if (result == Result::Skipped) return tr(STR_NONE_OPT);
  if (result == Result::Unsupported) return tr(STR_NOT_SUPPORTED);
  return errorString(result);
}

const char* StatsUploadClient::errorString(Result result) {
  switch (result) {
    case Result::Ok:
      return "";
    case Result::Auth:
      return tr(STR_AUTH_FAILED);
    case Result::Unsupported:
      return tr(STR_KOREADER_SYNC_HTTP_404);
    case Result::LowMemory:
      return tr(STR_KOREADER_SYNC_LOW_MEMORY);
    case Result::Network:
      return tr(STR_CONNECTION_FAILED);
    default:
      return tr(STR_SYNC_FAILED_MSG);
  }
}
