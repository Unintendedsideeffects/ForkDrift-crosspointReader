#pragma once

#include <Arduino.h>

namespace network {

struct NotesHttpResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  String body = "Unhandled notes request";

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

NotesHttpResult handleNotesEntryRequest(bool notesEnabled, const String& textArg);
NotesHttpResult handleNotesGetRequest(bool notesEnabled);
NotesHttpResult handleNotesSaveRequest(bool notesEnabled, bool hasBody, const String& body);

}  // namespace network
