#pragma once

#include <Arduino.h>

#include <string>

namespace network {

struct TodoPlannerHttpResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  String body = "Unhandled TODO planner request";
  std::string targetPath;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

TodoPlannerHttpResult handleTodoEntryRequest(bool plannerEnabled, bool markdownEnabled, const String& textArg,
                                             const String& typeArg, const std::string& today);

TodoPlannerHttpResult handleTodoTodayGetRequest(bool plannerEnabled, bool markdownEnabled, const std::string& today);

TodoPlannerHttpResult handleTodoTodaySaveRequest(bool plannerEnabled, bool markdownEnabled, bool hasBody,
                                                 const String& body, const std::string& today);

}  // namespace network
