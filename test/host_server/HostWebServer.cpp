#include "HostWebServer.h"

#include <httplib.h>

#include <algorithm>
#include <cctype>
#include <fstream>

#include "util/PathUtils.h"

class WebServer::Impl {
 public:
  httplib::Server server;
  bool configured = false;
};

namespace {

HTTPMethod toHttpMethod(const std::string& method) {
  if (method == "GET") return HTTP_GET;
  if (method == "POST") return HTTP_POST;
  if (method == "PUT") return HTTP_PUT;
  if (method == "DELETE") return HTTP_DELETE;
  return HTTP_ANY;
}

std::string normalizeContentType(std::string contentType) {
  const size_t semicolon = contentType.find(';');
  if (semicolon != std::string::npos) {
    contentType.erase(semicolon);
  }

  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  contentType.erase(contentType.begin(), std::find_if(contentType.begin(), contentType.end(), notSpace));
  contentType.erase(std::find_if(contentType.rbegin(), contentType.rend(), notSpace).base(), contentType.end());
  std::transform(contentType.begin(), contentType.end(), contentType.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return contentType;
}

void populateFormArgs(const std::string& body, std::map<std::string, String>& args) {
  size_t start = 0;
  while (start <= body.size()) {
    const size_t end = body.find('&', start);
    const std::string_view pair(body.data() + start, (end == std::string::npos ? body.size() : end) - start);
    const size_t eq = pair.find('=');
    const std::string_view rawKey = pair.substr(0, eq);
    const std::string_view rawValue = eq == std::string_view::npos ? std::string_view{} : pair.substr(eq + 1);
    args[PathUtils::urlDecode(String(std::string(rawKey).c_str())).toStdString()] =
        PathUtils::urlDecode(String(std::string(rawValue).c_str()));
    if (end == std::string::npos) break;
    start = end + 1;
  }
}

void populateMultipartArgs(const httplib::MultipartFormData& form, std::map<std::string, String>& args) {
  for (const auto& [key, field] : form.fields) {
    args[key] = String(field.content.c_str());
  }
}

std::string staticMimeType(const std::string& path) {
  const size_t dot = path.rfind('.');
  if (dot != std::string::npos) {
    const std::string ext = path.substr(dot);
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".css") return "text/css";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".json") return "application/json";
  }
  return "application/octet-stream";
}

void writeResponse(const WebServer::Response& response, size_t contentLength, httplib::Response& res) {
  res.status = response.statusCode >= 0 ? response.statusCode : 500;
  res.set_header("Access-Control-Allow-Origin", "*");
  if (!response.contentType.isEmpty()) {
    res.set_content(response.body.c_str(), response.body.length(), response.contentType.c_str());
  } else {
    res.body = response.body.c_str();
  }
  for (const auto& [key, value] : response.headers) {
    res.set_header(key.c_str(), value.c_str());
  }
  if (contentLength != CONTENT_LENGTH_UNKNOWN && contentLength > 0) {
    res.set_header("Content-Length", std::to_string(contentLength).c_str());
  }
}

}  // namespace

WebServer::WebServer() : impl_(std::make_unique<Impl>()) {}

WebServer::~WebServer() = default;

void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction handler) {
  routes_.push_back(Route{String(uri), method, std::move(handler), {}});
}

void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction handler, THandlerFunction uploadHandler) {
  routes_.push_back(Route{String(uri), method, std::move(handler), std::move(uploadHandler)});
}

void WebServer::on(const String& uri, HTTPMethod method, THandlerFunction handler) {
  routes_.push_back(Route{uri, method, std::move(handler), {}});
}

void WebServer::on(const String& uri, HTTPMethod method, THandlerFunction handler, THandlerFunction uploadHandler) {
  routes_.push_back(Route{uri, method, std::move(handler), std::move(uploadHandler)});
}

void WebServer::onNotFound(THandlerFunction handler) { notFoundHandler_ = std::move(handler); }

bool WebServer::hasArg(const String& name) const { return request_.args.count(name.toStdString()) > 0; }

String WebServer::arg(const String& name) const {
  const auto it = request_.args.find(name.toStdString());
  return it == request_.args.end() ? String() : it->second;
}

String WebServer::uri() const { return request_.uri; }

HTTPMethod WebServer::method() const { return request_.method; }

WiFiClient WebServer::client() const { return {}; }

const HTTPUpload& WebServer::upload() const { return upload_; }

void WebServer::send(int statusCode, const char* contentType, const String& body) {
  response_.statusCode = statusCode;
  response_.contentType = String(contentType ? contentType : "");
  response_.body = body;
}

void WebServer::send(int statusCode, const String& contentType, const String& body) {
  send(statusCode, contentType.c_str(), body);
}

void WebServer::sendHeader(const char* name, const char* value) {
  response_.headers[name ? name : ""] = String(value ? value : "");
}

void WebServer::sendHeader(const char* name, const String& value) { sendHeader(name, value.c_str()); }

void WebServer::send_P(int statusCode, const char* contentType, const char* body, size_t length) {
  response_.statusCode = statusCode;
  response_.contentType = String(contentType ? contentType : "");
  response_.body = String();
  if (body && length > 0) response_.body.write(reinterpret_cast<const uint8_t*>(body), length);
}

void WebServer::setContentLength(size_t length) { contentLength_ = length; }

void WebServer::sendContent(const char* body) { sendContent(body, body ? std::char_traits<char>::length(body) : 0); }

void WebServer::sendContent(const char* body, size_t length) {
  if (body && length > 0) response_.body.write(reinterpret_cast<const uint8_t*>(body), length);
}

void WebServer::sendContent(const String& body) { sendContent(body.c_str(), body.length()); }

void WebServer::ensureServer() {
  if (impl_->configured) return;
  impl_->configured = true;

  const auto handler = [this](const httplib::Request& req, httplib::Response& res) {
    Response response;
    dispatch(req, response);
    writeResponse(response, contentLength_, res);
  };

  impl_->server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.status = 204;
  });
  impl_->server.Get(R"(.*)", handler);
  impl_->server.Post(R"(.*)", handler);
  impl_->server.Put(R"(.*)", handler);
  impl_->server.Delete(R"(.*)", handler);
}

bool WebServer::dispatch(const httplib::Request& req, Response& out) {
  request_ = RequestState{};
  upload_ = HTTPUpload{};

  request_.method = toHttpMethod(req.method);
  request_.uri = String(req.path);
  for (const auto& [key, value] : req.params) request_.args[key] = String(value);

  const std::string normalizedContentType = normalizeContentType(req.get_header_value("Content-Type"));
  if (!req.body.empty() && normalizedContentType != "multipart/form-data") {
    request_.args["plain"] = String(req.body.c_str());
  }
  if (request_.method == HTTP_POST && normalizedContentType == "application/x-www-form-urlencoded") {
    populateFormArgs(req.body, request_.args);
  } else if (request_.method == HTTP_POST && normalizedContentType == "multipart/form-data") {
    populateMultipartArgs(req.form, request_.args);
  }

  response_ = Response{};
  contentLength_ = 0;

  for (const auto& route : routes_) {
    if ((route.method == request_.method || route.method == HTTP_ANY) && route.uri == request_.uri) {
      if (request_.method == HTTP_POST && normalizedContentType == "multipart/form-data" && route.uploadHandler) {
        for (const auto& [fieldName, file] : req.form.files) {
          upload_ = HTTPUpload{};
          upload_.status = UPLOAD_FILE_START;
          upload_.filename = String(file.filename.c_str());
          upload_.name = String(fieldName.c_str());
          upload_.type = String(file.content_type.c_str());
          route.uploadHandler();

          upload_.status = UPLOAD_FILE_WRITE;
          upload_.buf = reinterpret_cast<const uint8_t*>(file.content.data());
          upload_.currentSize = file.content.size();
          upload_.totalSize = file.content.size();
          route.uploadHandler();

          upload_.status = UPLOAD_FILE_END;
          upload_.buf = nullptr;
          upload_.currentSize = 0;
          upload_.totalSize = file.content.size();
          upload_.final = true;
          route.uploadHandler();
        }
      }
      route.handler();
      out = response_;
      return true;
    }
  }

  if (!htmlRoot_.empty() && request_.method == HTTP_GET) {
    const std::string uriStr = request_.uri.toStdString();
    if (uriStr.find("..") == std::string::npos) {
      std::ifstream file(htmlRoot_ + uriStr, std::ios::binary);
      if (file) {
        const std::string content((std::istreambuf_iterator<char>(file)), {});
        response_.statusCode = 200;
        response_.contentType = String(staticMimeType(uriStr).c_str());
        response_.body = String();
        response_.body.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
        out = response_;
        return true;
      }
    }
  }

  if (notFoundHandler_) {
    notFoundHandler_();
  } else {
    send(404, "text/plain", "Not found");
  }
  out = response_;
  return false;
}

bool WebServer::serveDirectory(const char* fsPath) {
  if (!fsPath || !*fsPath) return false;
  htmlRoot_ = fsPath;
  while (!htmlRoot_.empty() && htmlRoot_.back() == '/') htmlRoot_.pop_back();
  return true;
}

bool WebServer::listen(const char* host, int port) {
  ensureServer();
  return impl_->server.listen(host ? host : "127.0.0.1", port);
}

bool WebServer::listen(int port) { return listen("127.0.0.1", port); }

void WebServer::stop() { impl_->server.stop(); }
