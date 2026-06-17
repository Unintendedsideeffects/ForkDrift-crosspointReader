#pragma once

#include <WebServer.h>

namespace network {

struct UploadPostResult {
  int statusCode = 400;
  const char* contentType = "text/plain";
  String body;
  String filePath;
  String uploadPath;
  String fileName;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

void startUpload(WebServer* server);
UploadPostResult buildUploadPostResult();
void resetUploadSession();

}  // namespace network
