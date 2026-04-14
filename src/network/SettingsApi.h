#pragma once

#include <Arduino.h>

namespace network {

struct SettingsApplyResult {
  int statusCode;
  String contentType;
  String body;
  int appliedCount;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

String buildSettingsListJson();
SettingsApplyResult applySettingsJson(const String& body);

}  // namespace network
