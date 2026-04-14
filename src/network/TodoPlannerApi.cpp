#include "network/TodoPlannerApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <cctype>
#include <cstring>
#include <string>

#include "SpiBusMutex.h"
#include "activities/todo/TodoPlannerStorage.h"

namespace network {
namespace {

constexpr size_t TODO_ENTRY_MAX_TEXT_LENGTH = 300;

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
  if (trimmed.size() > TODO_ENTRY_MAX_TEXT_LENGTH) {
    trimmed.resize(TODO_ENTRY_MAX_TEXT_LENGTH);
  }
  return trimmed;
}

void appendTodoItemFromLine(JsonArray& array, std::string line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  if (line.empty()) {
    return;
  }

  JsonObject item = array.add<JsonObject>();
  if (line.rfind("- [ ] ", 0) == 0) {
    item["text"] = line.substr(6);
    item["type"] = "todo";
    item["checked"] = false;
    item["isHeader"] = false;
  } else if (line.rfind("- [x] ", 0) == 0 || line.rfind("- [X] ", 0) == 0) {
    item["text"] = line.substr(6);
    item["type"] = "todo";
    item["checked"] = true;
    item["isHeader"] = false;
  } else if (line.rfind("> ", 0) == 0) {
    item["text"] = line.substr(2);
    item["type"] = "agenda";
    item["checked"] = false;
    item["isHeader"] = true;
  } else {
    item["text"] = line;
    item["type"] = "text";
    item["checked"] = false;
    item["isHeader"] = true;
  }
}

TodoPlannerHttpResult plannerDisabled() { return {404, "text/plain", "TODO planner disabled", {}}; }

TodoPlannerHttpResult dateUnavailable() { return {503, "text/plain", "Date unavailable", {}}; }

}  // namespace

TodoPlannerHttpResult handleTodoEntryRequest(const bool plannerEnabled, const bool markdownEnabled, const String& textArg,
                                             const String& typeArg, const std::string& today) {
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
  if (text.isEmpty() || text.length() > TODO_ENTRY_MAX_TEXT_LENGTH) {
    return {400, "text/plain", "Invalid text", {}};
  }

  const bool agendaEntry = typeArg.equalsIgnoreCase("agenda");
  const std::string markdownPath = "/daily/" + today + ".md";
  const std::string textPath = "/daily/" + today + ".txt";
  const std::string dirPath = "/daily";

  std::string content;
  std::string targetPath;
  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    const bool markdownExists = Storage.exists(markdownPath.c_str());
    const bool textExists = Storage.exists(textPath.c_str());
    targetPath = TodoPlannerStorage::dailyPath(today, markdownEnabled, markdownExists, textExists);
    if (!Storage.exists(dirPath.c_str())) {
      Storage.mkdir(dirPath.c_str());
    }
    if (Storage.exists(targetPath.c_str())) {
      content = Storage.readFile(targetPath.c_str()).c_str();
      if (!content.empty() && content.back() != '\n') {
        content.push_back('\n');
      }
    }
    content += TodoPlannerStorage::formatEntry(text.c_str(), agendaEntry, markdownEnabled);
    content.push_back('\n');
    writeOk = Storage.writeFile(targetPath.c_str(), content.c_str());
  }

  if (!writeOk) {
    return {500, "text/plain", "Failed to write TODO entry", targetPath};
  }

  return {200, "application/json", "{\"ok\":true}", targetPath};
}

TodoPlannerHttpResult handleTodoTodayGetRequest(const bool plannerEnabled, const bool markdownEnabled,
                                                const std::string& today) {
  if (!plannerEnabled) {
    return plannerDisabled();
  }
  if (today.empty()) {
    return dateUnavailable();
  }

  const std::string markdownPath = "/daily/" + today + ".md";
  const std::string textPath = "/daily/" + today + ".txt";
  std::string targetPath;
  std::string content;
  {
    SpiBusMutex::Guard guard;
    const bool markdownExists = Storage.exists(markdownPath.c_str());
    const bool textExists = Storage.exists(textPath.c_str());
    targetPath = TodoPlannerStorage::dailyPath(today, markdownEnabled, markdownExists, textExists);
    if (Storage.exists(targetPath.c_str())) {
      content = Storage.readFile(targetPath.c_str()).c_str();
    }
  }

  JsonDocument response;
  response["ok"] = true;
  response["date"] = today.c_str();
  response["path"] = targetPath.c_str();
  JsonArray items = response["items"].to<JsonArray>();

  std::string line;
  line.reserve(128);
  for (const char c : content) {
    if (c == '\n') {
      appendTodoItemFromLine(items, line);
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  if (!line.empty()) {
    appendTodoItemFromLine(items, line);
  }

  String json;
  serializeJson(response, json);
  return {200, "application/json", json, targetPath};
}

TodoPlannerHttpResult handleTodoTodaySaveRequest(const bool plannerEnabled, const bool markdownEnabled, const bool hasBody,
                                                 const String& body, const std::string& today) {
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

  const std::string markdownPath = "/daily/" + today + ".md";
  const std::string textPath = "/daily/" + today + ".txt";
  const std::string dirPath = "/daily";
  std::string targetPath;
  std::string content;

  JsonArray items = request["items"].as<JsonArray>();
  for (JsonVariant itemVar : items) {
    if (!itemVar.is<JsonObject>()) {
      continue;
    }

    JsonObject item = itemVar.as<JsonObject>();
    const std::string text = normalizeTodoEntryText(item["text"].as<std::string>());
    if (text.empty()) {
      continue;
    }

    const bool isHeader = item["isHeader"].is<bool>() ? item["isHeader"].as<bool>() : item["is_header"].as<bool>();
    const bool checked = item["checked"].as<bool>();
    const char* itemType = item["type"] | "";
    const bool isAgenda = isHeader && std::strcmp(itemType, "agenda") == 0;

    if (isHeader) {
      if (isAgenda && markdownEnabled) {
        content += "> ";
      }
      content += text;
    } else {
      content += "- [";
      content += checked ? "x" : " ";
      content += "] ";
      content += text;
    }
    content.push_back('\n');
  }

  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    const bool markdownExists = Storage.exists(markdownPath.c_str());
    const bool textExists = Storage.exists(textPath.c_str());
    targetPath = TodoPlannerStorage::dailyPath(today, markdownEnabled, markdownExists, textExists);
    if (!Storage.exists(dirPath.c_str())) {
      Storage.mkdir(dirPath.c_str());
    }
    writeOk = Storage.writeFile(targetPath.c_str(), content.c_str());
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
