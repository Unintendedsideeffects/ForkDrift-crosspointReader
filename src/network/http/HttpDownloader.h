#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>

/**
 * HTTP client utility for fetching content and downloading files. GET/fetch and
 * file download use esp_http_client with 2KB TLS buffers. Server certificates are
 * not verified (no crt bundle), matching the prior setInsecure() behaviour.
 * postJson still uses the Arduino HTTPClient path.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  // Minimum free heap required before attempting an HTTPS connection. Below
  // this the aggregate mbedTLS allocations are likely to fail or cause heap
  // exhaustion. Exposed so callers can check and take action (e.g. restart)
  // before attempting a fetch rather than failing mid-request.
  static constexpr uint32_t MIN_HEAP_FOR_HTTPS = 38000;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    TIMEOUT,
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Probe a URL: sends GET with credentials, returns the HTTP status code
   * (or a negative HTTPClient error code) without reading the response body.
   * Useful for connection tests.
   */
  static int probeUrl(const std::string& url, const std::string& username = "", const std::string& password = "");

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "");

  /**
   * POST a JSON body to a URL and return the response body as a string.
   * Returns false on HTTP error or connection failure.
   */
  static bool postJson(const std::string& url, const std::string& body, std::string& outResponse);
};
