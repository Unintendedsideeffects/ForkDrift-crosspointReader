#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <base64.h>
#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>
#endif
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "AppVersion.h"
#include "network/DownloadFileSwap.h"
#include "network/HttpDownloadResume.h"
#include "network/HttpRedirectPolicy.h"
#include "network/WifiPowerSaveGuard.h"
#include "util/UrlUtils.h"

namespace {
constexpr size_t PROGRESS_UPDATE_BYTES = 64 * 1024;
constexpr uint32_t PROGRESS_UPDATE_MS = 250;
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 1024;
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr int HTTP_READ_POLL_TIMEOUT_MS = 5000;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
constexpr size_t DEFAULT_DOWNLOAD_BUFFER_SIZE = 2048;
constexpr uint8_t MAX_REDIRECTS = 5;

void logNetworkState(const char* phase) {
  LOG_DBG("HTTP", "%s: heap free=%u maxAlloc=%u wifi=%d rssi=%d", phase, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(WiFi.status()), WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
}

void logDownloadState(const char* phase, const size_t downloaded, const size_t total, const uint32_t idleMs) {
  LOG_ERR("HTTP", "%s after %zu/%zu bytes (idle=%lu ms, timeout=%lu ms)", phase, downloaded, total,
          static_cast<unsigned long>(idleMs), static_cast<unsigned long>(DOWNLOAD_IDLE_TIMEOUT_MS));
  logNetworkState(phase);
}

bool isRedirect(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

struct ResponseHeaders {
  std::string location;
  std::string range;
  std::string validator;
};

esp_err_t captureLocationHeader(esp_http_client_event_t* evt) {
  auto* headers = static_cast<ResponseHeaders*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_HEADER && headers && evt->header_key && evt->header_value) {
    if (strcasecmp(evt->header_key, "Location") == 0) headers->location.assign(evt->header_value);
    if (strcasecmp(evt->header_key, "ETag") == 0 && strncmp(evt->header_value, "W/", 2) != 0 &&
        strlen(evt->header_value) <= 128)
      headers->validator.assign(evt->header_value);
    if (strcasecmp(evt->header_key, "Last-Modified") == 0 && headers->validator.empty() &&
        strlen(evt->header_value) <= 128)
      headers->validator.assign(evt->header_value);
    // Range headers are tiny; malformed oversized values must not grow RAM.
    if (strcasecmp(evt->header_key, "Content-Range") == 0)
      headers->range.assign(evt->header_value, strnlen(evt->header_value, 96));
  }
  return ESP_OK;
}

bool isCancelRequested(bool* cancelFlag, const HttpDownloader::CancelCallback& shouldCancel) {
  if (cancelFlag && *cancelFlag) return true;
  if (shouldCancel && shouldCancel()) {
    if (cancelFlag) *cancelFlag = true;
    return true;
  }
  return false;
}

class ProgressNotifier {
 public:
  explicit ProgressNotifier(const HttpDownloader::ProgressCallback& progress) : progress_(&progress) {}

  void setTotal(const size_t total) { total_ = total; }

  void notify(size_t downloaded, bool force) {
    // Known gap: with no Content-Length (chunked or close-delimited bodies)
    // total_ stays 0 and callers never hear about progress, so the OPDS
    // download screen sits at 0% until the transfer ends. Cancel still works
    // through shouldCancel. If a server ever does this for books, report
    // bytes on a timer with total 0 and draw an indeterminate bar.
    if (!progress_ || !*progress_ || total_ == 0) return;

    const uint32_t now = millis();
    if (force || downloaded == total_ || downloaded - lastProgressBytes_ >= PROGRESS_UPDATE_BYTES ||
        now - lastProgressMs_ >= PROGRESS_UPDATE_MS) {
      lastProgressBytes_ = downloaded;
      lastProgressMs_ = now;
      (*progress_)(downloaded, total_);
    }
  }

 private:
  size_t total_ = 0;
  size_t lastProgressBytes_ = 0;
  uint32_t lastProgressMs_ = 0;
  const HttpDownloader::ProgressCallback* progress_ = nullptr;
};

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  HttpDownloader::CancelCallback shouldCancel;
  size_t resumeOffset = 0;
  size_t downloaded = 0;
  size_t total = 0;
  bool rangeIgnored = false;
  bool retryable = false;
  bool invalidResponse = false;
  size_t responseEnd = 0;
  std::string validator;
};

// Called before any response bytes reach the output, including empty bodies.
bool prepareResponse(Sink& sink, int status, std::string_view range, bool hasLength, size_t length,
                     const std::string& validator) {
  if (status == 200 && sink.resumeOffset > 0) {
    sink.rangeIgnored = true;
    return false;
  }
  if (status == 206) {
    if ((!sink.validator.empty() && !validator.empty() && sink.validator != validator) ||
        !HttpDownloadResume::range(range, sink.resumeOffset, sink.total, hasLength, length, sink.total,
                                   sink.responseEnd)) {
      LOG_ERR("HTTP", "Invalid resumed response");
      sink.invalidResponse = true;
      return false;
    }
  } else if (status == 200) {
    sink.total = hasLength ? length : 0;
    sink.responseEnd = sink.total;
    sink.validator = validator;
  } else {
    return false;
  }
  return true;
}

void setRequestHeaders(esp_http_client_handle_t client, const std::string& username, const std::string& password,
                       size_t resumeOffset, bool sendAuthorization) {
  esp_http_client_set_header(client, "User-Agent", AppVersion::userAgent());
  esp_http_client_set_header(client, "Connection", "close");
  if (resumeOffset > 0) {
    char rangeHeader[40];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%zu-", resumeOffset);
    esp_http_client_set_header(client, "Range", rangeHeader);
    LOG_DBG("HTTP", "Resuming download at byte %zu", resumeOffset);
  }
  if (sendAuthorization) {
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }
}

void logTlsError(esp_http_client_handle_t client, const char* phase) {
  int tlsError = 0;
  int tlsFlags = 0;
  const esp_err_t err = esp_http_client_get_and_clear_last_tls_error(client, &tlsError, &tlsFlags);
  if (err != ESP_OK || tlsError != 0 || tlsFlags != 0) {
    const int tlsCode = tlsError < 0 ? -tlsError : tlsError;
    LOG_ERR("HTTP", "%s TLS error: err=%s mbedtls=0x%x flags=0x%x", phase, esp_err_to_name(err), tlsCode, tlsFlags);
  }
}

#if defined(FREEINK_NET_WOLFSSL)
std::string responseValidator(freeink::SecureHttpClient& http) {
  std::string value = http.getHeader("etag");
  if (value.empty() || value.compare(0, 2, "W/") == 0 || value.size() > 128) value = http.getHeader("last-modified");
  if (value.size() > 128) value.clear();
  return value;
}
#endif

#if defined(FREEINK_NET_WOLFSSL)
HttpDownloader::DownloadError runGetWolfSsl(const std::string& url, const std::string& username,
                                            const std::string& password,
                                            const HttpRedirectPolicy::Url& credentialOrigin, const bool hasCredentials,
                                            Sink& sink, const size_t bufferSize) {
  (void)bufferSize;  // SecureHttpClient owns one fixed 1024-byte streaming buffer.
  std::string currentUrl = url;
  ProgressNotifier progressNotifier(sink.progress);

  for (uint8_t hop = 0; hop < MAX_REDIRECTS; ++hop) {
    HttpRedirectPolicy::Url currentOrigin;
    const bool currentParsed = HttpRedirectPolicy::parseUrl(currentUrl, currentOrigin);
    const bool sendAuthorization =
        currentParsed && HttpRedirectPolicy::shouldSendAuthorization(currentOrigin, credentialOrigin, hasCredentials);

    freeink::SecureHttpClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    // SecureNet does not yet expose ESP-IDF's CA bundle. This matches the
    // existing KOSync transport; cross-origin hops omit Basic credentials.
    http.setInsecure();
    if (!http.begin(currentUrl)) {
      LOG_ERR("HTTP", "wolfSSL rejected URL: %s", UrlUtils::forLog(currentUrl).c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    // Replace SecureHttpClient's built-in User-Agent so strict servers receive
    // exactly one header while retaining CrossInk's device/version identity.
    http.setUserAgent(AppVersion::userAgent());
    if (sink.resumeOffset > 0) {
      char rangeHeader[40];
      snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%zu-", sink.resumeOffset);
      http.addHeader("Range", rangeHeader);
      if (!sink.validator.empty()) http.addHeader("If-Range", sink.validator);
      LOG_DBG("HTTP", "Resuming download at byte %zu", sink.resumeOffset);
    }
    if (sendAuthorization) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    LOG_DBG("HTTP", "wolfSSL GET: %s", UrlUtils::forLog(currentUrl).c_str());
    bool prepared = false;
    const int status = http.GET(
        [&http, &sink, &progressNotifier, &prepared](const uint8_t* data, const size_t len) {
          const int responseStatus = http.getStatus();
          if (responseStatus != 200 && responseStatus != 206) return true;
          if (!prepared) {
            if (!prepareResponse(sink, responseStatus, http.getHeader("content-range"), http.hasContentLength(),
                                 http.getContentLength(), responseValidator(http)))
              return false;
            prepared = true;
            progressNotifier.setTotal(sink.total);
          }
          if (!HttpDownloadResume::accepts(sink.downloaded, sink.responseEnd, len)) {
            sink.invalidResponse = true;
            return false;
          }
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          progressNotifier.notify(sink.downloaded, false);
          return true;
        },
        [&sink]() { return isCancelRequested(sink.cancelFlag, sink.shouldCancel); });

    if (http.aborted()) return HttpDownloader::ABORTED;
    if (sink.rangeIgnored) {
      LOG_DBG("HTTP", "Server ignored range request; restarting download");
      return HttpDownloader::HTTP_ERROR;
    }
    if (sink.invalidResponse) return HttpDownloader::HTTP_ERROR;
    if (status < 0) {
      sink.retryable = !http.callbackAborted();
      LOG_ERR("HTTP", "wolfSSL request failed: %s", UrlUtils::forLog(currentUrl).c_str());
      logNetworkState("wolfSSL request failure");
      return HttpDownloader::HTTP_ERROR;
    }

    if (isRedirect(status)) {
      const std::string location = http.getHeader("location");
      if (location.empty()) {
        LOG_ERR("HTTP", "Redirect missing Location header");
        return HttpDownloader::HTTP_ERROR;
      }

      const std::string redirectUrl = HttpRedirectPolicy::buildRedirectUrl(currentUrl, location);
      HttpRedirectPolicy::Url redirect;
      if (!HttpRedirectPolicy::parseUrl(redirectUrl, redirect)) {
        LOG_ERR("HTTP", "Rejected redirect with unsupported Location");
        return HttpDownloader::HTTP_ERROR;
      }
      if (currentParsed && !HttpRedirectPolicy::isAllowedRedirect(currentOrigin, redirect)) {
        LOG_ERR("HTTP", "Rejected HTTPS downgrade redirect to %s", redirect.host.c_str());
        return HttpDownloader::HTTP_ERROR;
      }
      currentUrl = redirectUrl;
      LOG_DBG("HTTP", "Redirecting to: %s", redirect.host.c_str());
      continue;
    }

    if (!prepared && !prepareResponse(sink, status, http.getHeader("content-range"), http.hasContentLength(),
                                      http.getContentLength(), responseValidator(http))) {
      LOG_ERR("HTTP", "Rejected response: %d", status);
      return HttpDownloader::HTTP_ERROR;
    }
    if (http.callbackAborted()) {
      LOG_ERR("HTTP", "Write failed after %zu/%zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::FILE_ERROR;
    }
    if (!http.responseComplete() || (sink.total && sink.downloaded != sink.total)) {
      sink.retryable = true;
      LOG_ERR("HTTP", "Incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::HTTP_ERROR;
    }

    progressNotifier.notify(sink.downloaded, true);
    return HttpDownloader::OK;
  }

  LOG_ERR("HTTP", "Redirect limit exceeded");
  return HttpDownloader::HTTP_ERROR;
}
#endif

HttpDownloader::DownloadError runGetDefault(const std::string& url, const std::string& username,
                                            const std::string& password,
                                            const HttpRedirectPolicy::Url& credentialOrigin, const bool hasCredentials,
                                            Sink& sink, const size_t bufferSize) {
  std::string currentUrl = url;

  for (uint8_t hop = 0; hop < MAX_REDIRECTS; ++hop) {
    HttpRedirectPolicy::Url currentOrigin;
    const bool currentParsed = HttpRedirectPolicy::parseUrl(currentUrl, currentOrigin);
    const bool sendAuthorization =
        currentParsed && HttpRedirectPolicy::shouldSendAuthorization(currentOrigin, credentialOrigin, hasCredentials);
    ResponseHeaders responseHeaders;

    esp_http_client_config_t config = {};
    config.url = currentUrl.c_str();
    config.buffer_size = HTTP_RX_BUF;
    config.buffer_size_tx = HTTP_TX_BUF;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = false;
    config.event_handler = captureLocationHeader;
    config.user_data = &responseHeaders;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("HTTP", "Client init failed");
      logNetworkState("Client init failure");
      return HttpDownloader::HTTP_ERROR;
    }

    setRequestHeaders(client, username, password, sink.resumeOffset, sendAuthorization);
    if (sink.resumeOffset && !sink.validator.empty())
      esp_http_client_set_header(client, "If-Range", sink.validator.c_str());

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Open failed: %s", esp_err_to_name(err));
      sink.retryable = true;
      logTlsError(client, "Open failure");
      logNetworkState("Open failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    int64_t responseLength = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (responseLength < 0) {
      LOG_ERR("HTTP", "Fetch headers failed: %lld", static_cast<long long>(responseLength));
      sink.retryable = true;
      logNetworkState("Fetch headers failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    if (isRedirect(status)) {
      if (responseHeaders.location.empty()) {
        LOG_ERR("HTTP", "Redirect missing Location header");
        logNetworkState("Redirect missing Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }

      const std::string redirectUrl = HttpRedirectPolicy::buildRedirectUrl(currentUrl, responseHeaders.location);
      HttpRedirectPolicy::Url redirect;
      if (!HttpRedirectPolicy::parseUrl(redirectUrl, redirect)) {
        LOG_ERR("HTTP", "Rejected redirect with unsupported Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (currentParsed && !HttpRedirectPolicy::isAllowedRedirect(currentOrigin, redirect)) {
        LOG_ERR("HTTP", "Rejected HTTPS downgrade redirect to %s", redirect.host.c_str());
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      currentUrl = redirectUrl;
      LOG_DBG("HTTP", "Redirecting to: %s", redirect.host.c_str());
      esp_http_client_cleanup(client);
      continue;
    }

    if (static_cast<uint64_t>(responseLength) > std::numeric_limits<size_t>::max() ||
        !prepareResponse(sink, status, responseHeaders.range, responseLength > 0, static_cast<size_t>(responseLength),
                         responseHeaders.validator)) {
      LOG_ERR("HTTP", "Rejected response: %d", status);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
#ifdef ESP_ERR_HTTP_EAGAIN
    err = esp_http_client_set_timeout_ms(client, HTTP_READ_POLL_TIMEOUT_MS);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Failed to set read timeout: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
#endif

    auto buffer = makeUniqueNoThrow<char[]>(bufferSize);
    if (!buffer) {
      LOG_ERR("HTTP", "Failed to allocate %zu byte download buffer", bufferSize);
      logNetworkState("Download buffer allocation failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    ProgressNotifier progressNotifier(sink.progress);
    progressNotifier.setTotal(sink.total);
#ifdef ESP_ERR_HTTP_EAGAIN
    uint32_t lastReadMs = millis();
#endif
    while (true) {
      if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) {
        esp_http_client_cleanup(client);
        return HttpDownloader::ABORTED;
      }

      const int bytesRead = esp_http_client_read(client, buffer.get(), bufferSize);
      if (bytesRead < 0) {
#ifdef ESP_ERR_HTTP_EAGAIN
        if (bytesRead == -ESP_ERR_HTTP_EAGAIN) {
          const uint32_t idleMs = millis() - lastReadMs;
          if (idleMs >= DOWNLOAD_IDLE_TIMEOUT_MS) {
            sink.retryable = true;
            logDownloadState("Read timed out", sink.downloaded, sink.total, idleMs);
            esp_http_client_cleanup(client);
            return HttpDownloader::HTTP_ERROR;
          }
          delay(1);
          continue;
        }
#endif
        LOG_ERR("HTTP", "Read error after %zu/%zu bytes", sink.downloaded, sink.total);
        sink.retryable = true;
        logNetworkState("Read error");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (bytesRead == 0) break;

      if (!HttpDownloadResume::accepts(sink.downloaded, sink.responseEnd, static_cast<size_t>(bytesRead))) {
        LOG_ERR("HTTP", "Response exceeds its advertised range");
        sink.invalidResponse = true;
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (!sink.write(reinterpret_cast<const uint8_t*>(buffer.get()), static_cast<size_t>(bytesRead))) {
        LOG_ERR("HTTP", "Write failed after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Write failure");
        esp_http_client_cleanup(client);
        return HttpDownloader::FILE_ERROR;
      }

      sink.downloaded += static_cast<size_t>(bytesRead);
#ifdef ESP_ERR_HTTP_EAGAIN
      lastReadMs = millis();
#endif
      if (sink.total > 0 && sink.total <= PROGRESS_UPDATE_BYTES) {
      }
      progressNotifier.notify(sink.downloaded, false);
      if (sink.total > 0 && sink.downloaded >= sink.total) break;
      delay(0);
    }

    const bool complete = esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    progressNotifier.notify(sink.downloaded, true);
    if (!complete || (sink.total && sink.downloaded != sink.total)) {
      sink.retryable = true;
      LOG_ERR("HTTP", "Incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      logNetworkState("Incomplete transfer");
      return HttpDownloader::HTTP_ERROR;
    }

    return HttpDownloader::OK;
  }

  LOG_ERR("HTTP", "Redirect limit exceeded");
  logNetworkState("Redirect limit exceeded");
  return HttpDownloader::HTTP_ERROR;
}

HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     const std::string_view authorizationOrigin, Sink& sink, const size_t bufferSize,
                                     const HttpDownloader::Transport transport) {
  HttpRedirectPolicy::Url credentialOrigin;
  const std::string_view credentialUrl = authorizationOrigin.empty() ? std::string_view(url) : authorizationOrigin;
  const bool hasCredentials =
      !username.empty() && !password.empty() && HttpRedirectPolicy::parseUrl(credentialUrl, credentialOrigin);
#if defined(FREEINK_NET_WOLFSSL)
  if (transport == HttpDownloader::Transport::WOLFSSL) {
    return runGetWolfSsl(url, username, password, credentialOrigin, hasCredentials, sink, bufferSize);
  }
#else
  (void)transport;
#endif
  return runGetDefault(url, username, password, credentialOrigin, hasCredentials, sink, bufferSize);
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password);
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  outContent.clear();
  return fetchUrl(
      url,
      [&outContent](const uint8_t* data, size_t len) {
        outContent.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      username, password);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  return streamUrl(url, onData, nullptr, username, password) == OK;
}

HttpDownloader::DownloadError HttpDownloader::streamUrl(const std::string& url, const DataCallback& onData,
                                                        ProgressCallback progress, const std::string& username,
                                                        const std::string& password, DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  if (!onData) {
    LOG_ERR("HTTP", "Fetch failed: missing data callback");
    return HTTP_ERROR;
  }

  Sink sink;
  sink.write = onData;
  sink.progress = std::move(progress);
  sink.shouldCancel = std::move(options.shouldCancel);
  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  return runGet(url, username, password, options.authorizationOrigin, sink, bufferSize, options.transport);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  if (options.stageAsPart && !DownloadFileSwap::recover(destPath)) return FILE_ERROR;
  const std::string writePath = options.stageAsPart ? destPath + ".part" : destPath;
  size_t resumeOffset = 0;
  if (options.resumePartial && Storage.exists(writePath.c_str())) {
    FsFile existingFile;
    if (Storage.openFileForRead("HTTP", writePath.c_str(), existingFile)) {
      resumeOffset = existingFile.fileSize();
      existingFile.close();
    }
  }

  if (resumeOffset == 0 && Storage.exists(writePath.c_str())) {
    Storage.remove(writePath.c_str());
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.shouldCancel = std::move(options.shouldCancel);
  sink.resumeOffset = resumeOffset;

  FsFile file;
  bool fileOpen = false;
#ifndef SIMULATOR
  bool spaceChecked = false;
#endif
  bool insufficientSpace = false;
  auto openOutputFile = [&]() {
    if (fileOpen) return true;
#ifndef SIMULATOR
    // The host storage shim does not expose card capacity.
    if (options.checkFreeSpace && !spaceChecked && sink.total > 0) {
      spaceChecked = true;
      // Some SD transports cannot report capacity; let the write fail instead.
      const uint64_t totalBytes = Storage.totalBytes();
      if (totalBytes > 0 && sink.total > sink.resumeOffset) {
        const uint64_t usedBytes = Storage.usedBytes();
        const uint64_t freeBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
        const uint64_t neededBytes = sink.total - sink.resumeOffset;
        if (freeBytes < neededBytes) {
          LOG_ERR("HTTP", "Insufficient SD space: free=%llu required=%llu", static_cast<unsigned long long>(freeBytes),
                  static_cast<unsigned long long>(neededBytes));
          insufficientSpace = true;
          return false;
        }
      }
    }
#endif
    if (sink.resumeOffset > 0) {
      file = Storage.open(writePath.c_str(), O_WRONLY | O_APPEND);
    } else {
      fileOpen = Storage.openFileForWrite("HTTP", writePath.c_str(), file);
      if (!fileOpen) {
        LOG_ERR("HTTP", "Failed to open file for writing");
        return false;
      }
    }
    fileOpen = file;
    if (!fileOpen) {
      LOG_ERR("HTTP", "Failed to open file for writing");
    }
    return fileOpen;
  };

  sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };

  DownloadError result = HTTP_ERROR;
  HttpDownloadResume::RetryBudget budget;
  do {
    if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) {
      result = ABORTED;
      break;
    }
    const size_t before = sink.resumeOffset;
    sink.downloaded = before;
    sink.rangeIgnored = sink.retryable = sink.invalidResponse = false;
    sink.responseEnd = 0;
    result = runGet(url, username, password, options.authorizationOrigin, sink, bufferSize, options.transport);
    if (fileOpen) {
      const bool synced = file.sync();
      const bool closed = file.close();
      fileOpen = false;
      if (!synced || !closed) {
        LOG_ERR("HTTP", "Failed to finish downloaded file");
        result = FILE_ERROR;
      }
    }
    if (result != HTTP_ERROR || (!sink.retryable && !sink.rangeIgnored)) break;
    if (sink.rangeIgnored || (sink.downloaded > 0 && sink.validator.empty())) {
      // Without a version validator, reconnecting must restart to avoid mixing revisions.
      // Close before reopening: SdFat permits only one handle per path.
      if (Storage.exists(writePath.c_str()) && !Storage.remove(writePath.c_str())) {
        LOG_ERR("HTTP", "Failed to restart partial download");
        result = FILE_ERROR;
        break;
      }
      sink.downloaded = sink.total = 0;
#ifndef SIMULATOR
      spaceChecked = false;
#endif
    }
    if (!budget.again(before, sink.downloaded)) break;
    sink.resumeOffset = sink.downloaded;
    LOG_INF("HTTP", "Retrying download at byte %zu (attempt %u)", sink.resumeOffset, budget.attempts + 1);
    // Short, cancellable backoff on stalled connections; no extra task/buffer.
    for (unsigned i = 0; i < 10; ++i) {
      if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) break;
      delay(20);
    }
  } while (true);
  if (insufficientSpace) result = INSUFFICIENT_SPACE;

  if (result != OK) {
    LOG_ERR("HTTP", "Transfer failed: error=%d downloaded=%zu expected=%zu preservePartial=%d resumePartial=%d",
            static_cast<int>(result), sink.downloaded, sink.total, options.preservePartial, options.resumePartial);
    if (result == ABORTED || !options.preservePartial) {
      Storage.remove(writePath.c_str());
    }
    return result;
  }

  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    if (!options.preservePartial) {
      Storage.remove(writePath.c_str());
    }
    return HTTP_ERROR;
  }

  if (sink.total > 0 && sink.downloaded != sink.total) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", sink.downloaded, sink.total);
    if (!options.preservePartial) {
      Storage.remove(writePath.c_str());
    }
    return HTTP_ERROR;
  }

  if (options.validate && !options.validate(writePath)) {
    LOG_ERR("HTTP", "Downloaded file failed validation: %s", writePath.c_str());
    Storage.remove(writePath.c_str());
    return HTTP_ERROR;
  }

  if (options.stageAsPart && !DownloadFileSwap::publish(destPath)) return FILE_ERROR;

  return OK;
}
