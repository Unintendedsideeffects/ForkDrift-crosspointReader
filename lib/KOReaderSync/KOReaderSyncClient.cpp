#include "KOReaderSyncClient.h"

#include <ArduinoJson.h>
#include <HttpHeader.h>
#include <HttpOrigin.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <ctime>

#include "KOReaderCredentialStore.h"
#include "KOReaderProgressPolicy.h"

int KOReaderSyncClient::lastHttpCode = 0;

namespace {
// Device identifier for CrossPoint reader
constexpr char DEVICE_NAME[] = "CrossPoint";
constexpr char DEVICE_ID[] = "crosspoint-reader";

// Small TLS buffers to fit in ESP32-C3's limited heap (~46KB free after WiFi).
// KOSync payloads are tiny JSON (<1KB), so 2KB buffers are sufficient.
// Default 16KB buffers cause OOM during TLS handshake.
constexpr int HTTP_BUF_SIZE = 2048;

extern "C" esp_err_t arduino_esp_crt_bundle_attach(void* conf);
// Cloudflare tunnels send a 3-cert Google Trust Services chain. During the TLS handshake
// mbedTLS makes many small allocations that collectively consume ~48KB of heap. With only
// ~50KB free after WiFi connects, the session drove min-free-ever down to 2600 bytes before
// failing with MBEDTLS_ERR_X509_ALLOC_FAILED (-0x2880). Check total free heap (not max
// contiguous block) because the failure mode is aggregate exhaustion, not one large alloc.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;
constexpr int MAX_RESPONSE_BYTES = 16 * 1024;

// Response buffer for reading HTTP body
struct ResponseBuffer {
  char* data = nullptr;
  int len = 0;
  int capacity = 0;
  bool overflowed = false;

  ~ResponseBuffer() { free(data); }

  bool ensure(int size) {
    if (size <= capacity) return true;
    if (size > MAX_RESPONSE_BYTES) {
      overflowed = true;
      return false;
    }
    char* newData = (char*)realloc(data, size);
    if (!newData) return false;
    data = newData;
    capacity = size;
    return true;
  }
};

// HTTP event handler to collect response body
esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf) {
    // Redirects are followed by performKoSyncWithRedirects(), so intermediate 3xx
    // bodies arrive here too. Only 2xx bodies are ever parsed; collecting the rest
    // would prepend redirect HTML to the JSON, and a large error page could trip
    // the overflow cap and turn a 401 into SERVER_ERROR.
    const int status = esp_http_client_get_status_code(evt->client);
    if (status < 200 || status >= 300) return ESP_OK;
    if (buf->ensure(buf->len + evt->data_len + 1)) {
      memcpy(buf->data + buf->len, evt->data, evt->data_len);
      buf->len += evt->data_len;
      buf->data[buf->len] = '\0';
    } else {
      LOG_ERR("KOSync", "Response buffer rejected/failed (%d-byte chunk)", evt->data_len);
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

constexpr int kMaxRedirectUrl = 1024;
constexpr int kMaxRedirects = 5;

bool isRedirectStatus(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

void applyKoSyncHeaders(esp_http_client_handle_t client) {
  esp_http_client_set_header(client, "Accept", "application/vnd.koreader.v1+json");
  esp_http_client_set_header(client, "x-auth-user", KOREADER_STORE.getUsername().c_str());
  esp_http_client_set_header(client, "x-auth-key", KOREADER_STORE.getMd5Password().c_str());
  for (const auto& header : KOREADER_STORE.getCustomHeaders()) {
    if (header.name.empty()) {
      continue;
    }
    esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
  }
}

void dropKoSyncCredentialHeaders(esp_http_client_handle_t client) {
  esp_http_client_delete_header(client, "Authorization");
  esp_http_client_delete_header(client, "x-auth-user");
  esp_http_client_delete_header(client, "x-auth-key");
  for (const auto& header : KOREADER_STORE.getCustomHeaders()) {
    if (header.name.empty()) {
      continue;
    }
    LOG_INF("KOSync", "Redirect: dropping extra header %s", header.name.c_str());
    esp_http_client_delete_header(client, header.name.c_str());
  }
}

struct KoSyncRedirectGuard {
  std::string currentUrl;
  std::string credentialUrl;
};

esp_err_t followKoSyncRedirect(esp_http_client_handle_t client, KoSyncRedirectGuard& guard) {
  if (esp_http_client_set_redirection(client) != ESP_OK) {
    dropKoSyncCredentialHeaders(client);
    return ESP_FAIL;
  }
  auto next = makeUniqueNoThrow<char[]>(kMaxRedirectUrl);
  if (!next || esp_http_client_get_url(client, next.get(), kMaxRedirectUrl) != ESP_OK) {
    LOG_ERR("KOSync", "Redirect: could not read target URL");
    dropKoSyncCredentialHeaders(client);
    return ESP_FAIL;
  }

  http_origin::Origin from;
  http_origin::Origin to;
  const bool fromOk = http_origin::parse(guard.currentUrl, from);
  if (!http_origin::parse(next.get(), to)) {
    LOG_ERR("KOSync", "Redirect: unparseable target");
    dropKoSyncCredentialHeaders(client);
    return ESP_FAIL;
  }
  if (fromOk && http_origin::isDowngrade(from, to)) {
    LOG_ERR("KOSync", "Refusing HTTPS->HTTP redirect");
    dropKoSyncCredentialHeaders(client);
    return ESP_FAIL;
  }

  if (http_origin::extraHeadersFollowRedirect(guard.credentialUrl, next.get())) {
    applyKoSyncHeaders(client);
  } else {
    LOG_INF("KOSync", "Redirect to %.*s: not sending credentials to a different origin",
            static_cast<int>(to.host.size()), to.host.data());
    dropKoSyncCredentialHeaders(client);
  }
  guard.currentUrl.assign(next.get());
  return ESP_OK;
}

esp_err_t performKoSyncWithRedirects(esp_http_client_handle_t client, KoSyncRedirectGuard& guard) {
  for (int hop = 0;; ++hop) {
    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) return err;
    if (!isRedirectStatus(esp_http_client_get_status_code(client))) return ESP_OK;
    if (hop >= kMaxRedirects) {
      LOG_ERR("KOSync", "Too many redirects");
      return ESP_ERR_HTTP_MAX_REDIRECT;
    }
    if (followKoSyncRedirect(client, guard) != ESP_OK) return ESP_FAIL;
  }
}

esp_http_client_handle_t createClient(const char* url, ResponseBuffer* buf, KoSyncRedirectGuard& guard,
                                      esp_http_client_method_t method = HTTP_METHOD_GET) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.event_handler = httpEventHandler;
  config.user_data = buf;
  config.method = method;
  config.timeout_ms = 15000;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = HTTP_BUF_SIZE;
  config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
  config.disable_auto_redirect = true;

  config.username = KOREADER_STORE.getUsername().c_str();
  config.password = KOREADER_STORE.getPassword().c_str();
  config.auth_type = HTTP_AUTH_TYPE_BASIC;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return nullptr;

  if (esp_http_client_set_header(client, "Accept", "application/vnd.koreader.v1+json") != ESP_OK ||
      esp_http_client_set_header(client, "x-auth-user", KOREADER_STORE.getUsername().c_str()) != ESP_OK ||
      esp_http_client_set_header(client, "x-auth-key", KOREADER_STORE.getMd5Password().c_str()) != ESP_OK) {
    LOG_ERR("KOSync", "Failed to set auth headers");
    esp_http_client_cleanup(client);
    return nullptr;
  }

  applyKoSyncHeaders(client);
  guard.currentUrl = url;
  guard.credentialUrl = url;
  return client;
}
}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("KOSync", "Authenticating: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return LOW_MEMORY;
  }

  ResponseBuffer buf;
  KoSyncRedirectGuard guard;
  esp_http_client_handle_t client = createClient(url.c_str(), &buf, guard);
  if (!client) return NETWORK_ERROR;

  esp_err_t err = performKoSyncWithRedirects(client, guard);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Auth response: %d (err: %d)", httpCode, err);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (buf.overflowed) return SERVER_ERROR;
  // Any 2xx is success. The reference kosync server answers 200, but
  // KOSync-compatible implementations differ — BookLore is a Spring service
  // and uses the idiomatic codes.
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url =
      KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash + "?position_kinds=locator,percentage";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("KOSync", "Getting progress: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return LOW_MEMORY;
  }

  ResponseBuffer buf;
  KoSyncRedirectGuard guard;
  esp_http_client_handle_t client = createClient(url.c_str(), &buf, guard);
  if (!client) return NETWORK_ERROR;

  esp_err_t err = performKoSyncWithRedirects(client, guard);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Get progress response: %d (err: %d)", httpCode, err);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (buf.overflowed) return SERVER_ERROR;

  // 204 = success with no stored progress for this document (Spring-style
  // KOSync implementations; the reference server answers 200 with an empty
  // object instead). Map it to the same graceful no-remote-progress path as
  // 404 rather than letting it fall through to SERVER_ERROR.
  if (httpCode == 204) return NOT_FOUND;

  if (httpCode >= 200 && httpCode < 300 && buf.data) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buf.data);

    if (error) {
      LOG_ERR("KOSync", "JSON parse failed: %s", error.c_str());
      return JSON_ERROR;
    }

    const bool percentageNumeric = doc["percentage"].is<float>() || doc["percentage"].is<int>();
    const float percentage = percentageNumeric ? doc["percentage"].as<float>() : 0.f;
    if (!koSyncHasStoredProgress(doc["progress"].isNull(), percentageNumeric, percentage)) {
      LOG_DBG("KOSync", "No stored progress in successful response");
      return NOT_FOUND;
    }
    if (!percentageNumeric) {
      LOG_ERR("KOSync", "Response missing numeric 'percentage'");
      return JSON_ERROR;
    }
    if (!koSyncPercentageUsable(true, percentage)) {
      LOG_ERR("KOSync", "Response percentage is out of range");
      return JSON_ERROR;
    }
    if ((!doc["progress"].isNull() && !doc["progress"].is<const char*>()) ||
        (!doc["device"].isNull() && !doc["device"].is<const char*>()) ||
        (!doc["device_id"].isNull() && !doc["device_id"].is<const char*>()) ||
        (!doc["timestamp"].isNull() && !doc["timestamp"].is<int64_t>())) {
      LOG_ERR("KOSync", "Response field has unexpected type");
      return JSON_ERROR;
    }

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"] | std::string("");
    outProgress.percentage = percentage;
    outProgress.device = doc["device"].as<std::string>();
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    LOG_DBG("KOSync", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
    return OK;
  }

  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return NOT_FOUND;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("KOSync", "Updating progress: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return LOW_MEMORY;
  }

  // Build JSON body
  JsonDocument doc;
  doc["document"] = progress.document;
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = DEVICE_NAME;
  doc["device_id"] = DEVICE_ID;

  std::string body;
  serializeJson(doc, body);

  LOG_DBG("KOSync", "Request body: %s", body.c_str());

  ResponseBuffer buf;
  KoSyncRedirectGuard guard;
  esp_http_client_handle_t client = createClient(url.c_str(), &buf, guard, HTTP_METHOD_PUT);
  if (!client) return NETWORK_ERROR;

  if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
      esp_http_client_set_post_field(client, body.c_str(), body.length()) != ESP_OK) {
    LOG_ERR("KOSync", "Failed to set request body");
    esp_http_client_cleanup(client);
    return NETWORK_ERROR;
  }

  esp_err_t err = performKoSyncWithRedirects(client, guard);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Update progress response: %d (err: %d)", httpCode, err);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (buf.overflowed) return SERVER_ERROR;
  // Any 2xx accepts the progress. Spring-based KOSync implementations answer a
  // PUT with the idiomatic 201/204, which used to land in SERVER_ERROR and made
  // every push fail after a successful pull.
  if (httpCode >= 200 && httpCode < 300) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found";
    case LOW_MEMORY:
      return "Not enough memory for sync — please retry";
    default:
      return "Unknown error";
  }
}
