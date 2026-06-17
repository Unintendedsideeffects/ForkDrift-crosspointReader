#include "network/server/NotesApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <string>
#include <vector>

#include "SpiBusMutex.h"

namespace network {
namespace {

constexpr const char* kNotesFile = "/notes.txt";
constexpr size_t kMaxNoteLength = 120;
constexpr size_t kMaxNotes = 100;
constexpr size_t kMaxNotesFileBytes = 16u * 1024u;

std::string normalizeNoteText(const std::string& input) {
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
  if (trimmed.size() > kMaxNoteLength) {
    trimmed.resize(kMaxNoteLength);
  }
  return trimmed;
}

std::vector<std::string> readNotesLocked() {
  std::vector<std::string> notes;
  HalFile file;
  if (!Storage.openFileForRead("NOTES", kNotesFile, file)) {
    return notes;
  }

  const size_t size = static_cast<size_t>(file.fileSize64());
  if (size > kMaxNotesFileBytes) {
    LOG_ERR("NOTES", "Notes file too large (%zu bytes); ignoring", size);
    return notes;
  }

  std::string content;
  content.resize(size);
  if (size > 0) {
    const int read = file.read(&content[0], size);
    if (read < 0 || static_cast<size_t>(read) != size) {
      LOG_ERR("NOTES", "Notes file read short");
      return {};
    }
  }

  std::string line;
  line.reserve(kMaxNoteLength);
  for (const char c : content) {
    if (c == '\n' || c == '\r') {
      const std::string normalized = normalizeNoteText(line);
      if (!normalized.empty()) {
        notes.push_back(normalized);
        if (notes.size() >= kMaxNotes) {
          break;
        }
      }
      line.clear();
      continue;
    }
    line.push_back(c);
  }
  if (!line.empty() && notes.size() < kMaxNotes) {
    const std::string normalized = normalizeNoteText(line);
    if (!normalized.empty()) {
      notes.push_back(normalized);
    }
  }
  return notes;
}

bool writeNotesLocked(const std::vector<std::string>& notes) {
  String content;
#ifndef SIMULATOR
  content.reserve(notes.size() * 40);
#endif
  size_t count = 0;
  for (const std::string& note : notes) {
    if (count++ >= kMaxNotes) {
      break;
    }
    content += note.c_str();
    content += '\n';
  }
  return Storage.writeFile(kNotesFile, content);
}

NotesHttpResult notesDisabled() { return {404, "text/plain", "Notes disabled"}; }

}  // namespace

NotesHttpResult handleNotesEntryRequest(const bool notesEnabled, const String& textArg) {
  if (!notesEnabled) {
    return notesDisabled();
  }

  const std::string text = normalizeNoteText(textArg.c_str());
  if (text.empty()) {
    return {400, "text/plain", "Invalid text"};
  }

  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    std::vector<std::string> notes = readNotesLocked();
    notes.insert(notes.begin(), text);
    if (notes.size() > kMaxNotes) {
      notes.resize(kMaxNotes);
    }
    writeOk = writeNotesLocked(notes);
  }

  if (!writeOk) {
    return {500, "text/plain", "Failed to write note"};
  }
  return {200, "application/json", "{\"ok\":true}"};
}

NotesHttpResult handleNotesGetRequest(const bool notesEnabled) {
  if (!notesEnabled) {
    return notesDisabled();
  }

  std::vector<std::string> notes;
  {
    SpiBusMutex::Guard guard;
    notes = readNotesLocked();
  }

  JsonDocument response;
  response["ok"] = true;
  response["path"] = kNotesFile;
  JsonArray items = response["items"].to<JsonArray>();
  for (const std::string& note : notes) {
    JsonObject item = items.add<JsonObject>();
    item["text"] = note;
  }

  String json;
  serializeJson(response, json);
  return {200, "application/json", json};
}

NotesHttpResult handleNotesSaveRequest(const bool notesEnabled, const bool hasBody, const String& body) {
  if (!notesEnabled) {
    return notesDisabled();
  }
  if (!hasBody) {
    return {400, "text/plain", "Missing body"};
  }

  JsonDocument request;
  if (deserializeJson(request, body.c_str())) {
    return {400, "text/plain", "Invalid JSON body"};
  }
  if (!request["items"].is<JsonArray>()) {
    return {400, "text/plain", "Missing items array"};
  }

  std::vector<std::string> notes;
  JsonArray items = request["items"].as<JsonArray>();
  for (JsonVariant itemVar : items) {
    std::string text;
    if (itemVar.is<const char*>()) {
      text = normalizeNoteText(itemVar.as<const char*>());
    } else if (itemVar.is<JsonObject>()) {
      text = normalizeNoteText(itemVar.as<JsonObjectConst>()["text"].as<std::string>());
    }
    if (text.empty()) {
      continue;
    }
    notes.push_back(text);
    if (notes.size() >= kMaxNotes) {
      break;
    }
  }

  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    writeOk = writeNotesLocked(notes);
  }

  if (!writeOk) {
    return {500, "text/plain", "Failed to write notes"};
  }
  return {200, "application/json", "{\"ok\":true,\"path\":\"/notes.txt\"}"};
}

}  // namespace network
