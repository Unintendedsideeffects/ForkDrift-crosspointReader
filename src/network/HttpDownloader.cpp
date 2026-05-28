#include "HttpDownloader.h"

#include <HTTPClient.h>
#include <Logging.h>
#include <StreamString.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>

#include <cstring>
#include <memory>
#include <utility>

#include "SpiBusMutex.h"
#include "util/UrlUtils.h"

namespace {
class FileWriteStream final : public Stream {
 public:
  FileWriteStream(HalFile& file, const size_t total, HttpDownloader::ProgressCallback progress, bool* cancelFlag)
      : file_(file), total_(total), progress_(std::move(progress)), cancelFlag_(cancelFlag) {}

  size_t write(const uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, const size_t size) override {
    if (cancelFlag_ && *cancelFlag_) {
      writeOk_ = false;
      return 0;
    }
    SpiBusMutex::Guard guard;
    const size_t written = file_.write(buffer, size);
    if (written != size) {
      writeOk_ = false;
    }
    downloaded_ += written;
    if (progress_ && total_ > 0) {
      progress_(downloaded_, total_);
    }
    return written;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  void flush() override {
    SpiBusMutex::Guard guard;
    file_.flush();
  }

  size_t downloaded() const { return downloaded_; }
  bool ok() const { return writeOk_; }

 private:
  HalFile& file_;
  size_t total_;
  size_t downloaded_ = 0;
  bool writeOk_ = true;
  HttpDownloader::ProgressCallback progress_;
  bool* cancelFlag_;
};
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new WiFiClient());
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
  StreamString stream;
  if (!fetchUrl(url, stream, username, password)) {
    return false;
  }
  outContent = stream.c_str();
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
  std::unique_ptr<WiFiClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new (std::nothrow) WiFiClientSecure();
    if (!secureClient) {
      LOG_ERR("HTTP", "OOM: WiFiClientSecure (free heap: %u, largest block: %u)", ESP.getFreeHeap(),
              ESP.getMaxAllocHeap());
      return HTTP_ERROR;
    }
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new (std::nothrow) WiFiClient());
    if (!client) {
      LOG_ERR("HTTP", "OOM: WiFiClient (free heap: %u)", ESP.getFreeHeap());
      return HTTP_ERROR;
    }
  }
  HTTPClient http;

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }

  // Heap snapshot before the TLS handshake. A fresh WiFiClientSecure needs a
  // large *contiguous* block (~40KB) for mbedTLS; on this 380KB part the manifest
  // download can succeed while later .cpfont downloads fail with GET()==-1
  // (HTTPC_ERROR_CONNECTION_REFUSED) once the heap is fragmented. The largest
  // free block (not just total free) is what the handshake actually needs, so
  // logging both distinguishes OOM-starved TLS from a genuine network failure.
  LOG_DBG("HTTP", "Free heap before GET: %u (largest block: %u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Download failed: %d (free heap: %u, largest block: %u)", httpCode, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    http.end();
    return HTTP_ERROR;
  }

  const int64_t reportedLength = http.getSize();
  const size_t contentLength = reportedLength > 0 ? static_cast<size_t>(reportedLength) : 0;
  if (contentLength > 0) {
    LOG_DBG("HTTP", "Content-Length: %zu", contentLength);
  } else {
    LOG_DBG("HTTP", "Content-Length: unknown");
  }

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
      http.end();
      return FILE_ERROR;
    }
  }

  FileWriteStream fileStream(file, contentLength, std::move(progress), cancelFlag);
  const int writeResult = http.writeToStream(&fileStream);

  {
    SpiBusMutex::Guard guard;
    file.close();
  }
  http.end();

  if (cancelFlag && *cancelFlag) {
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return ABORTED;
  }

  if (writeResult < 0) {
    LOG_ERR("HTTP", "writeToStream error: %d", writeResult);
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  const size_t downloaded = fileStream.downloaded();
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);

  if (!fileStream.ok()) {
    LOG_ERR("HTTP", "Write failed during download");
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return FILE_ERROR;
  }

  if (contentLength == 0 && downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  if (contentLength > 0 && downloaded != contentLength) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, contentLength);
    SpiBusMutex::Guard guard;
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}
