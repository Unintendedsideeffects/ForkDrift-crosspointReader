#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "String.h"

namespace httplib {
struct Request;
}

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE };
inline constexpr size_t CONTENT_LENGTH_UNKNOWN = static_cast<size_t>(-1);

struct HTTPUpload {
  enum Status { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
  Status status = UPLOAD_FILE_START;
  String filename;
  String name;
  String type;
  size_t totalSize = 0;
  size_t currentSize = 0;
  const uint8_t* buf = nullptr;
  bool final = false;
};

inline constexpr auto UPLOAD_FILE_START = HTTPUpload::UPLOAD_FILE_START;
inline constexpr auto UPLOAD_FILE_WRITE = HTTPUpload::UPLOAD_FILE_WRITE;
inline constexpr auto UPLOAD_FILE_END = HTTPUpload::UPLOAD_FILE_END;
inline constexpr auto UPLOAD_FILE_ABORTED = HTTPUpload::UPLOAD_FILE_ABORTED;

class WiFiClient {
 public:
  size_t write(const uint8_t*, size_t len) { return len; }  // TODO: stream real socket writes for download routes.
  size_t write(const char* s) { return s ? std::char_traits<char>::length(s) : 0; }
  void flush() {}
};

class WebServer {
 public:
  using THandlerFunction = std::function<void()>;

  struct Response {
    int statusCode = -1;
    String contentType;
    String body;
    std::map<std::string, String> headers;
  };

  WebServer();
  ~WebServer();

  void on(const char* uri, HTTPMethod method, THandlerFunction handler);
  void on(const char* uri, HTTPMethod method, THandlerFunction handler, THandlerFunction uploadHandler);
  void on(const String& uri, HTTPMethod method, THandlerFunction handler);
  void on(const String& uri, HTTPMethod method, THandlerFunction handler, THandlerFunction uploadHandler);
  void onNotFound(THandlerFunction handler);

  bool hasArg(const String& name) const;
  String arg(const String& name) const;
  String uri() const;
  HTTPMethod method() const;
  WiFiClient client() const;
  const HTTPUpload& upload() const;

  void send(int statusCode, const char* contentType, const String& body);
  void send(int statusCode, const String& contentType, const String& body);
  void sendHeader(const char* name, const char* value);
  void sendHeader(const char* name, const String& value);
  void send_P(int statusCode, const char* contentType, const char* body, size_t length);
  void setContentLength(size_t length);
  void sendContent(const char* body);
  void sendContent(const char* body, size_t length);
  void sendContent(const String& body);

  bool listen(const char* host, int port);
  bool listen(int port);
  void stop();

  bool serveDirectory(const char* fsPath);

  const Response& response() const { return response_; }

 private:
  struct Route {
    String uri;
    HTTPMethod method;
    THandlerFunction handler;
    THandlerFunction uploadHandler;
  };

  struct RequestState {
    HTTPMethod method = HTTP_ANY;
    String uri;
    std::map<std::string, String> args;
  };

  void ensureServer();
  bool dispatch(const httplib::Request& req, Response& out);

  std::vector<Route> routes_;
  THandlerFunction notFoundHandler_;
  RequestState request_;
  Response response_;
  size_t contentLength_ = 0;
  HTTPUpload upload_;
  std::string htmlRoot_;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

using HostWebServer = WebServer;
