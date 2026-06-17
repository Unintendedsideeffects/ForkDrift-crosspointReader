#include "network/server/RemoteControlApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <cstring>

#include "SpiBusMutex.h"
#include "util/PathUtils.h"

namespace network {
namespace {

bool remoteOpenBookEnabled() { return true; }

bool remotePageTurnEnabled() { return true; }

OpenBookHttpResult toHttpResult(const OpenBookDecision& decision) {
  switch (decision.error) {
    case RemoteControlError::None:
      return {202, "application/json", "{\"status\":\"opening\"}", decision.path};
    case RemoteControlError::RemoteOpenBookDisabled:
      return {404, "text/plain", "Remote open-book disabled", {}};
    case RemoteControlError::MissingPath:
      return {400, "text/plain", "Missing path", {}};
    case RemoteControlError::InvalidPath:
      return {400, "text/plain", "Invalid path", {}};
    case RemoteControlError::FileNotFound:
      return {404, "text/plain", "File not found", {}};
    case RemoteControlError::MissingJsonBody:
      return {400, "text/plain", "Missing JSON body", {}};
    case RemoteControlError::InvalidJson:
      return {400, "text/plain", "Invalid JSON", {}};
    case RemoteControlError::RemotePageTurnDisabled:
    case RemoteControlError::UnknownButton:
      break;
  }
  return {};
}

RemoteButtonHttpResult toHttpResult(const RemoteButtonDecision& decision) {
  switch (decision.error) {
    case RemoteControlError::None:
      return {202, "application/json", "{\"status\":\"ok\"}", decision.pageTurn};
    case RemoteControlError::RemotePageTurnDisabled:
      return {404, "text/plain", "Remote page turn disabled", 0};
    case RemoteControlError::UnknownButton:
      return {400, "text/plain", "Unknown button; use page_forward or page_back", 0};
    case RemoteControlError::MissingJsonBody:
      return {400, "text/plain", "Missing JSON body", 0};
    case RemoteControlError::InvalidJson:
      return {400, "text/plain", "Invalid JSON", 0};
    case RemoteControlError::RemoteOpenBookDisabled:
    case RemoteControlError::MissingPath:
    case RemoteControlError::InvalidPath:
    case RemoteControlError::FileNotFound:
      break;
  }
  return {};
}

}  // namespace

OpenBookDecision evaluateOpenBookPath(const char* path) {
  if (!remoteOpenBookEnabled()) {
    return {RemoteControlError::RemoteOpenBookDisabled, {}};
  }
  if (path == nullptr || path[0] == '\0') {
    return {RemoteControlError::MissingPath, {}};
  }
  if (!PathUtils::isValidSdPath(String(path))) {
    return {RemoteControlError::InvalidPath, {}};
  }
  // Reject protected paths (e.g. /.crosspoint cache) so this endpoint can't be
  // used as an existence oracle to enumerate reading history.
  if (PathUtils::pathContainsProtectedItem(path)) {
    return {RemoteControlError::InvalidPath, {}};
  }

  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(path);
  }
  if (!exists) {
    return {RemoteControlError::FileNotFound, {}};
  }

  return {RemoteControlError::None, path};
}

RemoteButtonDecision evaluateRemoteButton(const char* button) {
  if (!remotePageTurnEnabled()) {
    return {RemoteControlError::RemotePageTurnDisabled, 0};
  }
  if (button == nullptr) {
    button = "";
  }
  if (std::strcmp(button, "page_forward") == 0 || std::strcmp(button, "next") == 0) {
    return {RemoteControlError::None, 1};
  }
  if (std::strcmp(button, "page_back") == 0 || std::strcmp(button, "prev") == 0 ||
      std::strcmp(button, "previous") == 0) {
    return {RemoteControlError::None, -1};
  }
  return {RemoteControlError::UnknownButton, 0};
}

OpenBookHttpResult parseOpenBookHttpRequest(const bool hasBody, const String& body) {
  if (!hasBody) {
    return {400, "text/plain", "Missing JSON body", {}};
  }

  JsonDocument doc;
  if (deserializeJson(doc, body.c_str())) {
    return {400, "text/plain", "Invalid JSON", {}};
  }

  return toHttpResult(evaluateOpenBookPath(doc["path"] | ""));
}

RemoteButtonHttpResult parseRemoteButtonHttpRequest(const bool hasBody, const String& body) {
  if (!hasBody) {
    return {400, "text/plain", "Missing JSON body", 0};
  }

  JsonDocument doc;
  if (deserializeJson(doc, body.c_str())) {
    return {400, "text/plain", "Invalid JSON", 0};
  }

  return toHttpResult(evaluateRemoteButton(doc["button"] | ""));
}

const char* toUsbError(const RemoteControlError error) {
  switch (error) {
    case RemoteControlError::RemoteOpenBookDisabled:
      return "remote_open_book disabled";
    case RemoteControlError::RemotePageTurnDisabled:
      return "remote_page_turn disabled";
    case RemoteControlError::MissingPath:
      return "missing path";
    case RemoteControlError::InvalidPath:
      return "invalid path";
    case RemoteControlError::FileNotFound:
      return "file not found";
    case RemoteControlError::UnknownButton:
      return "unknown button; use page_forward or page_back";
    case RemoteControlError::MissingJsonBody:
    case RemoteControlError::InvalidJson:
    case RemoteControlError::None:
      return nullptr;
  }
  return nullptr;
}

}  // namespace network
