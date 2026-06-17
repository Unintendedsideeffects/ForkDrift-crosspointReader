#pragma once

#include <Arduino.h>

#include <cstdint>
#include <string>

namespace network {

enum class RemoteControlError {
  None,
  RemoteOpenBookDisabled,
  RemotePageTurnDisabled,
  MissingJsonBody,
  InvalidJson,
  MissingPath,
  InvalidPath,
  FileNotFound,
  UnknownButton,
};

struct OpenBookDecision {
  RemoteControlError error = RemoteControlError::None;
  std::string path;

  bool ok() const { return error == RemoteControlError::None; }
};

struct RemoteButtonDecision {
  RemoteControlError error = RemoteControlError::None;
  int8_t pageTurn = 0;

  bool ok() const { return error == RemoteControlError::None; }
};

struct OpenBookHttpResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  const char* body = "Unhandled open-book request";
  std::string path;
};

struct RemoteButtonHttpResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  const char* body = "Unhandled remote button request";
  int8_t pageTurn = 0;
};

OpenBookDecision evaluateOpenBookPath(const char* path);
RemoteButtonDecision evaluateRemoteButton(const char* button);

OpenBookHttpResult parseOpenBookHttpRequest(bool hasBody, const String& body);
RemoteButtonHttpResult parseRemoteButtonHttpRequest(bool hasBody, const String& body);

const char* toUsbError(RemoteControlError error);

}  // namespace network
