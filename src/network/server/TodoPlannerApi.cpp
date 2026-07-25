#include "network/server/TodoPlannerApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstring>
#include <string>

#include "SpiBusMutex.h"
#include "activities/todo/TodoItem.h"
#include "activities/todo/TodoPlannerStorage.h"

namespace network {

namespace {

constexpr size_t kMaxTodoItems = 256;

// Writes a daily planner file atomically via a temporary file and rename.
// Caller already holds SpiBusMutex.
bool writeDailyFileAtomic(const std::string& targetPath, const std::string& content) {
  const std::string tempPath = targetPath + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite("WEB", tempPath.c_str(), file)) {
    return false;
  }
  const size_t bytesToWrite = content.size();
  if (bytesToWrite > 0 &&
      static_cast<size_t>(file.write(reinterpret_cast<const uint8_t*>(content.data()), bytesToWrite)) != bytesToWrite) {
    file.close();
    Storage.remove(tempPath.c_str());
    return false;
  }
  file.close();

  if (Storage.exists(targetPath.c_str())) {
    Storage.remove(targetPath.c_str());
  }
  if (!Storage.rename(tempPath.c_str(), targetPath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

std::string normalizeTodoEntryText(const std::string& input) {
  std::string normalized;
  normalized.reserve(input.size());

  for (const char c : input) {
    normalized.push_back((c == '\r' || c == '\n') ? ' ' : c);
  }

  size_t start = 0;
  while (start < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[start]))) {
    start++;
  }
  size_t end = normalized.size();
  while (end > start && std::isspace(static_cast<unsigned char>(normalized[end - 1]))) {
    end--;
  }

  std::string trimmed = normalized.substr(start, end - start);
  if (trimmed.size() > TodoPlannerStorage::kTodoEntryMaxTextLength) {
    trimmed.resize(TodoPlannerStorage::kTodoEntryMaxTextLength);
  }
  return trimmed;
}

const char* priorityToJson(const TodoPriority priority) {
  switch (priority) {
    case TodoPriority::P1:
      return "p1";
    case TodoPriority::P2:
      return "p2";
    case TodoPriority::P3:
      return "p3";
    case TodoPriority::None:
      break;
  }
  return "none";
}

TodoPriority priorityFromJson(const JsonObjectConst item) {
  const char* value = item["priority"] | "none";
  if (std::strcmp(value, "p1") == 0) {
    return TodoPriority::P1;
  }
  if (std::strcmp(value, "p2") == 0) {
    return TodoPriority::P2;
  }
  if (std::strcmp(value, "p3") == 0) {
    return TodoPriority::P3;
  }
  return TodoPriority::None;
}

void appendTodoItemJson(JsonArray& array, const TodoItem& todoItem) {
  JsonObject item = array.add<JsonObject>();
  item["text"] = todoItem.text;
  item["checked"] = todoItem.checked;
  item["isHeader"] = todoItem.isHeader;
  if (todoItem.isSection) {
    item["isSection"] = true;
  }
  if (todoItem.priority != TodoPriority::None) {
    item["priority"] = priorityToJson(todoItem.priority);
  }
  if (todoItem.dueMinutes != 0) {
    item["dueMinutes"] = todoItem.dueMinutes;
  }
  if (todoItem.recurrence != TodoRecurrence::None) {
    switch (todoItem.recurrence) {
      case TodoRecurrence::Daily:
        item["recurrence"] = "daily";
        break;
      case TodoRecurrence::Weekly:
        item["recurrence"] = "weekly";
        break;
      case TodoRecurrence::Weekdays:
        item["recurrence"] = "weekdays";
        item["weekdayMask"] = todoItem.weekdayMask;
        break;
      case TodoRecurrence::None:
        break;
    }
  }

  if (todoItem.isHeader) {
    if (todoItem.isSection) {
      item["type"] = "section";
    } else if (todoItem.isAgenda) {
      item["type"] = "agenda";
    } else {
      item["type"] = "text";
    }
  } else {
    item["type"] = "todo";
  }
}

TodoItem todoItemFromJson(const JsonObjectConst item) {
  TodoItem todoItem;
  todoItem.text = normalizeTodoEntryText(item["text"].as<std::string>());
  todoItem.checked = item["checked"] | false;
  todoItem.isHeader = item["isHeader"] | false;
  todoItem.isSection = item["isSection"] | false;
  todoItem.priority = priorityFromJson(item);
  todoItem.dueMinutes = item["dueMinutes"] | static_cast<uint16_t>(0);

  const char* recStr = item["recurrence"] | "none";
  if (std::strcmp(recStr, "daily") == 0) {
    todoItem.recurrence = TodoRecurrence::Daily;
  } else if (std::strcmp(recStr, "weekly") == 0) {
    todoItem.recurrence = TodoRecurrence::Weekly;
  } else if (std::strcmp(recStr, "weekdays") == 0) {
    todoItem.recurrence = TodoRecurrence::Weekdays;
    todoItem.weekdayMask = item["weekdayMask"] | static_cast<uint8_t>(0);
  }

  const char* itemType = item["type"] | "";
  if (todoItem.isHeader) {
    if (todoItem.isSection || std::strcmp(itemType, "section") == 0) {
      todoItem.isSection = true;
      todoItem.isAgenda = false;
    } else if (std::strcmp(itemType, "agenda") == 0) {
      todoItem.isAgenda = true;
    }
  }
  return todoItem;
}

TodoPlannerHttpResult plannerDisabled() { return {404, "text/plain", "TODO planner disabled", {}}; }

TodoPlannerHttpResult dateUnavailable() { return {503, "text/plain", "Date unavailable", {}}; }

}  // namespace

TodoPlannerHttpResult handleTodoEntryRequest(const bool plannerEnabled, const bool markdownEnabled,
                                             const String& textArg, const String& typeArg, const std::string& today) {
  if (!plannerEnabled) {
    return plannerDisabled();
  }
  if (today.empty()) {
    return dateUnavailable();
  }

  String text = textArg;
  text.replace("\r\n", " ");
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.trim();
  if (text.isEmpty() || text.length() > TodoPlannerStorage::kTodoEntryMaxTextLength) {
    return {400, "text/plain", "Invalid text", {}};
  }

  const bool agendaEntry = typeArg.equalsIgnoreCase("agenda");
  const std::string dirPath = "/daily";

  std::string content;
  std::string targetPath;
  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    targetPath = TodoPlannerStorage::resolveDailyPath(today, markdownEnabled);
    if (!Storage.exists(dirPath.c_str())) {
      if (!Storage.mkdir(dirPath.c_str())) {
        LOG_ERR("WEB", "Failed to create daily directory: %s", dirPath.c_str());
      }
    }
    if (Storage.exists(targetPath.c_str())) {
      content = TodoPlannerStorage::readDailyFileCapped(targetPath);
      if (!content.empty() && content.back() != '\n') {
        content.push_back('\n');
      }
    }
    content += TodoPlannerStorage::formatEntry(text.c_str(), agendaEntry, markdownEnabled);
    content.push_back('\n');
    writeOk = writeDailyFileAtomic(targetPath, content);
  }

  if (!writeOk) {
    return {500, "text/plain", "Failed to write TODO entry", targetPath};
  }

  return {200, "application/json", "{\"ok\":true}", targetPath};
}

TodoPlannerHttpResult handleTodoTodayGetRequest(const bool plannerEnabled, const bool markdownEnabled,
                                                const std::string& today) {
  (void)markdownEnabled;
  if (!plannerEnabled) {
    return plannerDisabled();
  }
  if (today.empty()) {
    return dateUnavailable();
  }

  std::string targetPath;
  std::string content;
  {
    SpiBusMutex::Guard guard;
    targetPath = TodoPlannerStorage::resolveDailyPath(today, markdownEnabled);
    if (Storage.exists(targetPath.c_str())) {
      content = TodoPlannerStorage::readDailyFileCapped(targetPath);
    }
  }

  std::vector<TodoItem> items;
  TodoPlannerStorage::parseFile(content, items);

  JsonDocument response;
  response["ok"] = true;
  response["date"] = today.c_str();
  response["path"] = targetPath.c_str();
  JsonArray jsonItems = response["items"].to<JsonArray>();
  for (const TodoItem& item : items) {
    appendTodoItemJson(jsonItems, item);
  }

  String json;
  serializeJson(response, json);
  return {200, "application/json", json, targetPath};
}

TodoPlannerHttpResult handleTodoTodaySaveRequest(const bool plannerEnabled, const bool markdownEnabled,
                                                 const bool hasBody, const String& body, const std::string& today) {
  if (!plannerEnabled) {
    return plannerDisabled();
  }
  if (!hasBody) {
    return {400, "text/plain", "Missing body", {}};
  }
  if (today.empty()) {
    return dateUnavailable();
  }

  JsonDocument request;
  if (deserializeJson(request, body.c_str())) {
    return {400, "text/plain", "Invalid JSON body", {}};
  }
  if (!request["items"].is<JsonArray>()) {
    return {400, "text/plain", "Missing items array", {}};
  }

  const std::string dirPath = "/daily";
  std::string targetPath;

  std::vector<TodoItem> items;
  JsonArray jsonItems = request["items"].as<JsonArray>();
  for (JsonVariant itemVar : jsonItems) {
    if (!itemVar.is<JsonObject>()) {
      continue;
    }
    JsonObjectConst jsonItem = itemVar.as<JsonObjectConst>();
    if (!jsonItem["is_header"].isNull()) {
      return {400, "text/plain", "Use isHeader", {}};
    }
    TodoItem item = todoItemFromJson(jsonItem);
    if (item.text.empty()) {
      continue;
    }
    if (items.size() >= kMaxTodoItems) {
      return {400, "text/plain", "Too many items", {}};
    }
    items.push_back(std::move(item));
  }

  const bool markdownFile = markdownEnabled;
  const std::string content = TodoPlannerStorage::formatFile(items, markdownFile);

  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    targetPath = TodoPlannerStorage::resolveDailyPath(today, markdownEnabled);
    if (!Storage.exists(dirPath.c_str())) {
      if (!Storage.mkdir(dirPath.c_str())) {
        LOG_ERR("WEB", "Failed to create daily directory: %s", dirPath.c_str());
      }
    }
    writeOk = writeDailyFileAtomic(targetPath, content);
  }

  if (!writeOk) {
    return {500, "text/plain", "Failed to write TODO file", targetPath};
  }

  JsonDocument response;
  response["ok"] = true;
  response["date"] = today.c_str();
  response["path"] = targetPath.c_str();
  String json;
  serializeJson(response, json);
  return {200, "application/json", json, targetPath};
}

}  // namespace network
