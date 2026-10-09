#include "network/http/HttpDownloader.h"

#include <HTTPClient.h>
#include <Logging.h>
#include <Memory.h>
#include <StreamString.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

#include "SpiBusMutex.h"
#include "network/http/HttpOrigin.h"
#include "util/TimeSync.h"
#include "util/UrlUtils.h"

namespace {
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

constexpr int kTlsBufferSize = 1024;

// Total free-heap floor before attempting an HTTPS handshake. The aggregate of
// many small mbedTLS allocations (not the largest block) is what matters here.
// Kept below the ~50KB seen during font downloads so it only rejects genuinely
// starved cases instead of viable ones.
constexpr uint32_t kMinHeapForTls = HttpDownloader::MIN_HEAP_FOR_HTTPS;

http_fetch::Reason admitHttps(const std::string& url) {
  if (!UrlUtils::isHttpsUrl(url)) {
    return http_fetch::Reason::Ok;
  }
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForTls) {
    LOG_WRN("HTTP", "Fetch failed: %s (free %u < %u, largest: %u)",
            http_fetch::reasonName(http_fetch::Reason::LowMemory), freeHeap, kMinHeapForTls, ESP.getMaxAllocHeap());
    return http_fetch::Reason::LowMemory;
  }
  TimeSync::ensureTrustedClock();
  return http_fetch::Reason::Ok;
}

using EspHttpEventHandler = decltype(esp_http_client_config_t::event_handler);

esp_http_client_config_t fillEspHttpGetConfig(const char* url, EspHttpEventHandler handler, void* userData,
                                              int timeoutMs) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.event_handler = handler;
  config.user_data = userData;
  config.timeout_ms = timeoutMs;
  config.buffer_size = kTlsBufferSize;
  config.buffer_size_tx = kTlsBufferSize;
  config.user_agent = "CrossPoint-ESP32-" CROSSPOINT_VERSION;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.max_authorization_retries = -1;
  // Redirects are followed by performWithRedirects(), not by the client: its
  // automatic redirect replays every custom header, so OPDS Basic credentials
  // would go to whatever host a redirect names.
  config.disable_auto_redirect = true;
  return config;
}

// Credential scoping across redirects (upstream d6f7f2565, in our transport).
// The Authorization header rides only on hops to the origin the credentials
// were configured for; HTTPS->HTTP downgrades are not followed at all.
struct RedirectGuard {
  std::string currentUrl;
  std::string credentialUrl;
  std::string authHeader;  // empty when the request has no credentials
};

constexpr int kMaxRedirectUrl = 1024;  // OPDS URLs are capped at 768 chars

esp_err_t followRedirect(esp_http_client_handle_t client, RedirectGuard& guard) {
  const bool hasCredentials = !guard.authHeader.empty();
  const auto dropCredentials = [&] {
    if (hasCredentials) esp_http_client_delete_header(client, "Authorization");
  };

  if (esp_http_client_set_redirection(client) != ESP_OK) {
    dropCredentials();
    return ESP_FAIL;
  }
  // Cold path (once per redirect hop): heap rather than a 1 KB stack buffer.
  auto next = makeUniqueNoThrow<char[]>(kMaxRedirectUrl);
  if (!next || esp_http_client_get_url(client, next.get(), kMaxRedirectUrl) != ESP_OK) {
    LOG_ERR("HTTP", "Redirect: could not read target URL");
    dropCredentials();
    return ESP_FAIL;
  }

  http_origin::Origin from;
  http_origin::Origin to;
  const bool fromOk = http_origin::parse(guard.currentUrl, from);
  if (!http_origin::parse(next.get(), to)) {
    LOG_ERR("HTTP", "Redirect: unparseable target");
    dropCredentials();
    return ESP_FAIL;
  }
  if (fromOk && http_origin::isDowngrade(from, to)) {
    LOG_ERR("HTTP", "Refusing HTTPS->HTTP redirect to %.*s", static_cast<int>(to.host.size()), to.host.data());
    dropCredentials();
    return ESP_FAIL;
  }

  if (hasCredentials) {
    http_origin::Origin credentialOrigin;
    if (http_origin::parse(guard.credentialUrl, credentialOrigin) && http_origin::sameOrigin(credentialOrigin, to)) {
      esp_http_client_set_header(client, "Authorization", guard.authHeader.c_str());
    } else {
      LOG_INF("HTTP", "Redirect to %.*s: not sending credentials to a different origin",
              static_cast<int>(to.host.size()), to.host.data());
      esp_http_client_delete_header(client, "Authorization");
    }
  }
  guard.currentUrl.assign(next.get());
  return ESP_OK;
}

constexpr int kMaxRedirects = 5;

bool isRedirectStatus(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// esp_http_client_perform() with redirects followed through followRedirect().
// With disable_auto_redirect the client returns each 3xx to us; the handlers
// ignore non-2xx bodies and headers, so only the final response reaches the sink.
esp_err_t performWithRedirects(esp_http_client_handle_t client, RedirectGuard& guard) {
  for (int hop = 0;; ++hop) {
    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) return err;
    if (!isRedirectStatus(esp_http_client_get_status_code(client))) return ESP_OK;
    if (hop >= kMaxRedirects) {
      LOG_ERR("HTTP", "Too many redirects");
      return ESP_ERR_HTTP_MAX_REDIRECT;
    }
    if (followRedirect(client, guard) != ESP_OK) return ESP_FAIL;
  }
}

// Initialises guard for a request to url and attaches Basic credentials, if any.
void applyCredentials(esp_http_client_handle_t client, RedirectGuard& guard, const std::string& url,
                      const std::string& username, const std::string& password) {
  guard.currentUrl = url;
  guard.credentialUrl = url;
  if (username.empty()) return;
  const std::string credentials = username + ":" + password;
  String encoded = base64::encode(credentials.c_str());
  encoded.trim();
  guard.authHeader = std::string("Basic ") + encoded.c_str();
  esp_http_client_set_header(client, "Authorization", guard.authHeader.c_str());
}

// Carries download state into the esp_http_client event handler.
struct DownloadContext {
  HalFile* file = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  bool writeOk = true;
  bool aborted = false;
  RedirectGuard redirect;
};

esp_err_t downloadEventHandler(esp_http_client_event_t* evt) {
  auto* ctx = static_cast<DownloadContext*>(evt->user_data);
  if (!ctx) return ESP_OK;

  switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
      // Only the final 2xx response's length counts, not an intermediate redirect's.
      if (evt->header_key && evt->header_value && strcasecmp(evt->header_key, "Content-Length") == 0) {
        const int status = esp_http_client_get_status_code(evt->client);
        if (status >= 200 && status < 300) ctx->total = static_cast<size_t>(strtoul(evt->header_value, nullptr, 10));
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

struct FetchContext {
  HttpDownloader::DataCallback onData;
  Stream* stream = nullptr;
  bool writeOk = true;
  bool aborted = false;
  size_t expectedBody = 0;
  size_t receivedBody = 0;
  RedirectGuard redirect;
};

esp_err_t fetchEventHandler(esp_http_client_event_t* evt) {
  auto* ctx = static_cast<FetchContext*>(evt->user_data);
  if (!ctx) return ESP_OK;

  switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
      // Only the final 2xx response's length counts, not an intermediate redirect's.
      if (evt->header_key && evt->header_value && strcasecmp(evt->header_key, "Content-Length") == 0) {
        const int status = esp_http_client_get_status_code(evt->client);
        if (status >= 200 && status < 300) {
          ctx->expectedBody = static_cast<size_t>(strtoul(evt->header_value, nullptr, 10));
        }
      }
      return ESP_OK;

    case HTTP_EVENT_ON_DATA: {
      const int status = esp_http_client_get_status_code(evt->client);
      if (status < 200 || status >= 300) return ESP_OK;

      const auto* data = static_cast<const uint8_t*>(evt->data);
      const size_t len = static_cast<size_t>(evt->data_len);
      if (len == 0) return ESP_OK;
      ctx->receivedBody += len;

      if (ctx->onData) {
        if (!ctx->onData(data, len)) {
          ctx->aborted = true;
          return ESP_FAIL;
        }
      } else if (ctx->stream) {
        const size_t written = ctx->stream->write(data, len);
        if (written != len) {
          ctx->writeOk = false;
          return ESP_FAIL;
        }
      }
      return ESP_OK;
    }

    default:
      return ESP_OK;
  }
}

void logFetchFailure(const http_fetch::Result& result, bool logFailure, const uint32_t freeHeap,
                     const uint32_t largestBlock) {
  const char* name = http_fetch::reasonName(result.reason);
  if (!logFailure) {
    LOG_DBG("HTTP", "Fetch failed: %s (status %d, free heap: %u, largest block: %u)", name, result.httpStatus, freeHeap,
            largestBlock);
    return;
  }
  if (http_fetch::logAsError(result.reason)) {
    LOG_ERR("HTTP", "Fetch failed: %s (status %d, free heap: %u, largest block: %u)", name, result.httpStatus, freeHeap,
            largestBlock);
    return;
  }
  LOG_WRN("HTTP", "Fetch failed: %s (status %d, free heap: %u, largest block: %u)", name, result.httpStatus, freeHeap,
          largestBlock);
}

http_fetch::Result espFetch(const std::string& url, FetchContext& ctx, const std::string& username,
                            const std::string& password, bool logFailure) {
  const http_fetch::Reason admit = admitHttps(url);
  if (admit != http_fetch::Reason::Ok) {
    return http_fetch::Result{admit, -1};
  }

  esp_http_client_config_t config = fillEspHttpGetConfig(url.c_str(), fetchEventHandler, &ctx, 15000);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    const http_fetch::Result result{http_fetch::Reason::AllocationFailure, -1};
    logFetchFailure(result, logFailure, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return result;
  }

  applyCredentials(client, ctx.redirect, url, username, password);

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  const esp_err_t err = performWithRedirects(client, ctx.redirect);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  http_fetch::Result result;
  result.httpStatus = status;
  result.reason = http_fetch::classify(http_fetch::ClassifyInput{
      .https = UrlUtils::isHttpsUrl(url),
      .admitted = true,
      .clientAllocated = true,
      .transportErr = static_cast<int>(err),
      .httpStatus = status,
      .writeOk = ctx.writeOk,
      .aborted = ctx.aborted,
      .expectedBody = ctx.expectedBody,
      .receivedBody = ctx.receivedBody,
      .freeHeap = ESP.getFreeHeap(),
      .minHeapForTls = kMinHeapForTls,
  });

  if (!result.ok()) {
    logFetchFailure(result, logFailure, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return result;
  }

  LOG_DBG("HTTP", "Fetch success");
  return result;
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password, int* outStatus, bool logFailure) {
  const http_fetch::Result result = fetchUrlResult(url, outContent, username, password, logFailure);
  if (outStatus) *outStatus = result.httpStatus;
  return result.ok();
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password, int* outStatus, bool logFailure) {
  const http_fetch::Result result = fetchUrlResult(url, onData, username, password, logFailure);
  if (outStatus) *outStatus = result.httpStatus;
  return result.ok();
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password, int* outStatus, bool logFailure) {
  const http_fetch::Result result = fetchUrlResult(url, outContent, username, password, logFailure);
  if (outStatus) *outStatus = result.httpStatus;
  return result.ok();
}

http_fetch::Result HttpDownloader::fetchUrlResult(const std::string& url, Stream& outContent,
                                                  const std::string& username, const std::string& password,
                                                  bool logFailure) {
  FetchContext ctx;
  ctx.stream = &outContent;
  return espFetch(url, ctx, username, password, logFailure);
}

http_fetch::Result HttpDownloader::fetchUrlResult(const std::string& url, const DataCallback& onData,
                                                  const std::string& username, const std::string& password,
                                                  bool logFailure) {
  FetchContext ctx;
  ctx.onData = onData;
  return espFetch(url, ctx, username, password, logFailure);
}

http_fetch::Result HttpDownloader::fetchUrlResult(const std::string& url, std::string& outContent,
                                                  const std::string& username, const std::string& password,
                                                  bool logFailure) {
  constexpr size_t kMaxBodyBytes = 64u * 1024u;
  BoundedStringSink sink(kMaxBodyBytes);
  http_fetch::Result result = fetchUrlResult(url, static_cast<Stream&>(sink), username, password, logFailure);
  if (!result.ok()) {
    return result;
  }
  if (sink.overflowed()) {
    result.reason = http_fetch::Reason::IncompleteBody;
    LOG_WRN("HTTP", "Fetch failed: %s (status %d, free heap: %u, largest block: %u)",
            http_fetch::reasonName(result.reason), result.httpStatus, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return result;
  }
  outContent = std::move(sink.data());
  return result;
}

int HttpDownloader::probeUrl(const std::string& url, const std::string& username, const std::string& password) {
  if (admitHttps(url) != http_fetch::Reason::Ok) {
    return -1;
  }

  FetchContext ctx;
  esp_http_client_config_t config = fillEspHttpGetConfig(url.c_str(), fetchEventHandler, &ctx, 8000);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return -1;
  }

  applyCredentials(client, ctx.redirect, url, username, password);

  (void)performWithRedirects(client, ctx.redirect);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  LOG_DBG("HTTP", "Probe %s → %d", url.c_str(), status);
  return status;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password) {
  if (admitHttps(url) != http_fetch::Reason::Ok) {
    return HTTP_ERROR;
  }

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());
  LOG_DBG("HTTP", "Free heap before GET: %u (largest block: %u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Download beside the destination and only replace it once the transfer is
  // verified complete: removing destPath up front meant a failed or cancelled
  // re-download destroyed the copy the user already had (crossink 1d954ae34).
  const std::string partPath = destPath + ".part";
  {
    SpiBusMutex::Guard guard;
    if (Storage.exists(partPath.c_str())) {
      Storage.remove(partPath.c_str());  // stale partial from an interrupted download
    }
  }

  HalFile file;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
      LOG_ERR("HTTP", "Failed to open file for writing");
      return FILE_ERROR;
    }
  }

  DownloadContext ctx;
  ctx.file = &file;
  ctx.progress = std::move(progress);
  ctx.cancelFlag = cancelFlag;

  esp_http_client_config_t config = fillEspHttpGetConfig(url.c_str(), downloadEventHandler, &ctx, 15000);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "esp_http_client_init failed (free heap: %u)", ESP.getFreeHeap());
    SpiBusMutex::Guard guard;
    file.close();
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }

  applyCredentials(client, ctx.redirect, url, username, password);

  const esp_err_t err = performWithRedirects(client, ctx.redirect);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  {
    SpiBusMutex::Guard guard;
    file.close();
  }

  if (ctx.aborted || (cancelFlag && *cancelFlag)) {
    SpiBusMutex::Guard guard;
    Storage.remove(partPath.c_str());
    return ABORTED;
  }

  if (err != ESP_OK) {
    LOG_ERR("HTTP", "Download failed: %s (status %d, free heap: %u, largest block: %u)", esp_err_to_name(err), status,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    SpiBusMutex::Guard guard;
    Storage.remove(partPath.c_str());
    return (err == ESP_ERR_HTTP_EAGAIN) ? TIMEOUT : HTTP_ERROR;
  }

  if (status != 200) {
    LOG_ERR("HTTP", "Download failed: HTTP %d", status);
    SpiBusMutex::Guard guard;
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }

  if (!ctx.writeOk) {
    LOG_ERR("HTTP", "Write failed during download");
    SpiBusMutex::Guard guard;
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }

  const size_t downloaded = ctx.downloaded;
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);

  if (downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    SpiBusMutex::Guard guard;
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }

  if (ctx.total > 0 && downloaded != ctx.total) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, ctx.total);
    SpiBusMutex::Guard guard;
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }

  {
    SpiBusMutex::Guard guard;
    if (Storage.exists(destPath.c_str())) {
      Storage.remove(destPath.c_str());
    }
    if (!Storage.rename(partPath.c_str(), destPath.c_str())) {
      LOG_ERR("HTTP", "Failed to move %s into place", partPath.c_str());
      Storage.remove(partPath.c_str());
      return FILE_ERROR;
    }
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
