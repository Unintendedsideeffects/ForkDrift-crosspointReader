#include "network/http/HttpDownloader.h"

#include <HTTPClient.h>
#include <Logging.h>
#include <StreamString.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "SpiBusMutex.h"
#include "util/UrlUtils.h"

namespace {
// Small TLS read/write buffers. The Arduino WiFiClientSecure path used the
// 16KB mbedTLS defaults, which need a ~40KB *contiguous* heap block for the
// handshake; once the heap fragments (e.g. after parsing a multi-family font
// manifest) that block no longer exists and GET() fails with -1. esp_http_client
// with small buffers shrinks the requirement to a few KB. Server certificates
// are intentionally not verified (no crt bundle), matching the prior
// setInsecure() behaviour: these are public assets and the device clock is not
// reliably NTP-synced at download time, which breaks certificate validity checks.
constexpr int kTlsBufferSize = 2048;

// Total free-heap floor before attempting an HTTPS handshake. The aggregate of
// many small mbedTLS allocations (not the largest block) is what matters here.
// Kept below the ~50KB seen during font downloads so it only rejects genuinely
// starved cases instead of viable ones.
constexpr uint32_t kMinHeapForTls = HttpDownloader::MIN_HEAP_FOR_HTTPS;

// Carries download state into the esp_http_client event handler.
struct DownloadContext {
  HalFile* file = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  bool writeOk = true;
  bool aborted = false;
};

esp_err_t downloadEventHandler(esp_http_client_event_t* evt) {
  auto* ctx = static_cast<DownloadContext*>(evt->user_data);
  if (!ctx) return ESP_OK;

  switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
      if (evt->header_key && evt->header_value && strcasecmp(evt->header_key, "Content-Length") == 0) {
        ctx->total = static_cast<size_t>(strtoul(evt->header_value, nullptr, 10));
      }
      return ESP_OK;

    case HTTP_EVENT_ON_DATA: {
      // Ignore bodies of intermediate redirect responses; only the final 2xx
      // payload should land on disk.
      const int status = esp_http_client_get_status_code(evt->client);
      if (status < 200 || status >= 300) return ESP_OK;

      if (ctx->cancelFlag && *ctx->cancelFlag) {
        ctx->aborted = true;
        return ESP_FAIL;
      }

      {
        SpiBusMutex::Guard guard;
        const size_t want = static_cast<size_t>(evt->data_len);
        const size_t written = ctx->file->write(static_cast<const uint8_t*>(evt->data), want);
        if (written != want) ctx->writeOk = false;
        ctx->downloaded += written;
      }

      if (ctx->progress && ctx->total > 0) ctx->progress(ctx->downloaded, ctx->total);

      if (ctx->cancelFlag && *ctx->cancelFlag) {
        ctx->aborted = true;
        return ESP_FAIL;
      }
      return ESP_OK;
    }

    default:
      return ESP_OK;
  }
}

// Buffers an HTTP body into a std::string but stops storing past maxBytes_,
// flagging overflow. Still claims full consumption so HTTPClient drains the
// socket cleanly. Bounds peak RAM on a 380KB device where an unbounded
// StreamString could OOM (and bare new on OOM aborts).
class BoundedStringSink final : public Stream {
 public:
  explicit BoundedStringSink(size_t maxBytes) : maxBytes_(maxBytes) {}

  size_t write(const uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, const size_t size) override {
    const size_t room = (data_.size() < maxBytes_) ? (maxBytes_ - data_.size()) : 0;
    const size_t take = (size < room) ? size : room;
    if (size > room) {
      overflowed_ = true;
    }
    if (take > 0) {
      data_.append(reinterpret_cast<const char*>(buffer), take);
    }
    return size;  // claim full consumption so writeToStream keeps draining
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  bool overflowed() const { return overflowed_; }
  std::string& data() { return data_; }

 private:
  size_t maxBytes_;
  std::string data_;
  bool overflowed_ = false;
};
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    // Guard against TLS allocation failing silently when heap is too fragmented.
    // The aggregate mbedTLS allocs (not just the largest block) matter here;
    // the same threshold is used in downloadToFile for the same reason.
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < kMinHeapForTls) {
      LOG_ERR("HTTP", "Heap too low for TLS fetch (%u < %u, largest: %u)", freeHeap, kMinHeapForTls,
              ESP.getMaxAllocHeap());
      return false;
    }
    auto* secureClient = new (std::nothrow) WiFiClientSecure();
    if (!secureClient) {
      LOG_ERR("HTTP", "OOM: WiFiClientSecure");
      return false;
    }
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new (std::nothrow) WiFiClient());
    if (!client) {
      LOG_ERR("HTTP", "OOM: WiFiClient");
      return false;
    }
  }
  HTTPClient http;

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Fetch failed: %d", httpCode);
    http.end();
    return false;
  }

  http.writeToStream(&outContent);
  http.end();

  LOG_DBG("HTTP", "Fetch success");
  return true;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  // Cap the in-RAM body. The only caller is the OPDS OpenSearch-description
  // fetch (small XML); 64KB is generous while protecting the heap against a
  // misbehaving or hostile server pushing a huge document.
  constexpr size_t kMaxBodyBytes = 64u * 1024u;
  BoundedStringSink sink(kMaxBodyBytes);
  if (!fetchUrl(url, sink, username, password)) {
    return false;
  }
  if (sink.overflowed()) {
    LOG_ERR("HTTP", "Response exceeded %u-byte cap; rejecting", static_cast<unsigned>(kMaxBodyBytes));
    return false;
  }
  outContent = std::move(sink.data());
  return true;
}

int HttpDownloader::probeUrl(const std::string& url, const std::string& username, const std::string& password) {
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new (std::nothrow) WiFiClientSecure();
    if (!secureClient) return HTTPC_ERROR_NO_HTTP_SERVER;
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new (std::nothrow) WiFiClient());
    if (!client) return HTTPC_ERROR_NO_HTTP_SERVER;
  }
  HTTPClient http;
  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  http.setTimeout(8000);
  if (!username.empty()) {
    const std::string credentials = username + ":" + password;
    http.addHeader("Authorization", "Basic " + base64::encode(credentials.c_str()));
  }
  const int code = http.GET();
  http.end();
  LOG_DBG("HTTP", "Probe %s → %d", url.c_str(), code);
  return code;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password) {
  const bool isHttps = UrlUtils::isHttpsUrl(url);

  if (isHttps) {
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < kMinHeapForTls) {
      LOG_ERR("HTTP", "Insufficient heap for TLS: %u free (need %u, largest block: %u)", freeHeap, kMinHeapForTls,
              ESP.getMaxAllocHeap());
      return HTTP_ERROR;
    }
  }

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());
  LOG_DBG("HTTP", "Free heap before GET: %u (largest block: %u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  {
    SpiBusMutex::Guard guard;
    if (Storage.exists(destPath.c_str())) {
      Storage.remove(destPath.c_str());
    }
  }

  HalFile file;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
      LOG_ERR("HTTP", "Failed to open file for writing");
      return FILE_ERROR;
    }
  }

  DownloadContext ctx;
  ctx.file = &file;
  ctx.progress = std::move(progress);
  ctx.cancelFlag = cancelFlag;

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.event_handler = downloadEventHandler;
  config.user_data = &ctx;
  config.timeout_ms = 15000;
  config.buffer_size = kTlsBufferSize;
  config.buffer_size_tx = kTlsBufferSize;
  config.user_agent = "CrossPoint-ESP32-" CROSSPOINT_VERSION;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "esp_http_client_init failed (free heap: %u)", ESP.getFreeHeap());
    SpiBusMutex::Guard guard;
    file.close();
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  if (!username.empty() && !password.empty()) {
    const std::string credentials = username + ":" + password;
    const String encoded = base64::encode(credentials.c_str());
    const std::string authHeader = std::string("Basic ") + encoded.c_str();
    esp_http_client_set_header(client, "Authorization", authHeader.c_str());
  }

  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  {
    SpiBusMutex::Guard guard;
    file.close();
  }

  if (ctx.aborted || (cancelFlag && *cancelFlag)) {
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return ABORTED;
  }

  if (err != ESP_OK) {
    LOG_ERR("HTTP", "Download failed: %s (status %d, free heap: %u, largest block: %u)", esp_err_to_name(err), status,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return (err == ESP_ERR_HTTP_EAGAIN) ? TIMEOUT : HTTP_ERROR;
  }

  if (status != 200) {
    LOG_ERR("HTTP", "Download failed: HTTP %d", status);
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  if (!ctx.writeOk) {
    LOG_ERR("HTTP", "Write failed during download");
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return FILE_ERROR;
  }

  const size_t downloaded = ctx.downloaded;
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);

  if (downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  if (ctx.total > 0 && downloaded != ctx.total) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, ctx.total);
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}

bool HttpDownloader::postJson(const std::string& url, const std::string& body, std::string& outResponse) {
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new (std::nothrow) WiFiClientSecure();
    if (!secureClient) {
      LOG_ERR("HTTP", "OOM: WiFiClientSecure for POST");
      return false;
    }
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new (std::nothrow) WiFiClient());
    if (!client) {
      LOG_ERR("HTTP", "OOM: WiFiClient for POST");
      return false;
    }
  }

  HTTPClient http;
  http.begin(*client, url.c_str());
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  LOG_DBG("HTTP", "POST %s (%zu bytes)", url.c_str(), body.size());

  const int httpCode = http.POST(body.c_str());
  if (httpCode < 200 || httpCode >= 300) {
    LOG_ERR("HTTP", "POST failed: %d", httpCode);
    http.end();
    return false;
  }

  constexpr size_t kMaxPostResponseBytes = 64u * 1024u;
  BoundedStringSink sink(kMaxPostResponseBytes);
  const int writeResult = http.writeToStream(&sink);
  if (writeResult < 0 || sink.overflowed()) {
    LOG_ERR("HTTP", "POST response rejected: write=%d overflow=%d", writeResult, sink.overflowed() ? 1 : 0);
    http.end();
    return false;
  }
  outResponse = std::move(sink.data());
  http.end();
  LOG_DBG("HTTP", "POST success (%d), response: %zu bytes", httpCode, outResponse.size());
  return true;
}
