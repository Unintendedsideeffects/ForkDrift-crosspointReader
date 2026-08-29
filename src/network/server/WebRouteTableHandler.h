#pragma once

#include <WebServer.h>

#include <cstddef>

class CrossPointWebServer;

class WebRouteTableHandler : public RequestHandler {
 public:
  explicit WebRouteTableHandler(CrossPointWebServer* owner);

  bool canHandle(HTTPMethod method, String uri) override;
  bool canUpload(String uri) override;
  bool handle(WebServer& server, HTTPMethod requestMethod, String requestUri) override;
  void upload(WebServer& server, String requestUri, HTTPUpload& upload) override;

 private:
  struct CoreRoute {
    const char* uri;
    HTTPMethod method;
    void (*fn)(CrossPointWebServer*);
    void (*ufn)(CrossPointWebServer*);
  };

  template <auto Method>
  static void invoke(CrossPointWebServer* self) {
    (self->*Method)();
  }

  static bool coreMatches(const CoreRoute& route, HTTPMethod method, const char* uri);
  static const CoreRoute* matchCore(HTTPMethod method, const char* uri);

  CrossPointWebServer* owner;
  static const CoreRoute kCore[];
  static const size_t kCoreCount;
};
