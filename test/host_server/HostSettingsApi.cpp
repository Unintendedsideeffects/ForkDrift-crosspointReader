#include "HostSettingsApi.h"

#include <ArduinoJson.h>

#include <cstring>

#include "HostStorage.h"
#include "network/SettingsSnapshotApi.h"
#include "src/CrossPointSettings.h"

namespace host {
namespace {

struct HostSetting {
  const char* key;
  const char* name;
  const char* category;
  const char* type;
  int defaultValue;
  const char* const* options;
  size_t optionCount;
  int min;
  int max;
  int step;
  const char* visibleKey;
  int visibleEq;
};

const char* kSleepScreenOptions[] = {"Dark", "Light", "Follow Theme", "Custom", "Transparent", "Smart"};
const char* kSleepSourceOptions[] = {"Sleep", "Pokedex", "All"};
const char* kFitCropOptions[] = {"Fit", "Crop"};
const char* kSleepFilterOptions[] = {"None", "Contrast", "Inverted"};
const char* kCycleOptions[] = {"Random", "Sequential"};
const char* kStatusBarOptions[] = {"Book", "Chapter", "Hide"};
const char* kThicknessOptions[] = {"Thin", "Medium", "Thick"};
const char* kHideBatteryOptions[] = {"Never", "In Reader", "Always"};
const char* kRefreshOptions[] = {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"};
const char* kThemeOptions[] = {"Classic", "Lyra", "Lyra Extended", "Fork Drift"};
const char* kFontOptions[] = {"Noto Serif", "Noto Sans", "OpenDyslexic"};
const char* kSizeOptions[] = {"Small", "Medium", "Large", "X-Large"};
const char* kSpacingOptions[] = {"Tight", "Normal", "Wide"};
const char* kAlignOptions[] = {"Justify", "Left", "Center", "Right", "Book style"};
const char* kImageOptions[] = {"Display", "Placeholder", "Suppress"};
const char* kOrientationOptions[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
const char* kSideButtonOptions[] = {"Prev / Next", "Next / Prev"};
const char* kPowerButtonOptions[] = {"Ignore", "Sleep", "Page turn", "Select", "Force refresh"};
const char* kSleepTimeoutOptions[] = {"1 minute", "5 minutes", "10 minutes", "15 minutes", "30 minutes"};
const char* kBackgroundServerOptions[] = {"Never", "Only on charge", "Always"};

constexpr HostSetting kSettings[] = {
    {"sleepScreen", "Sleep screen", "Display", "enum", 0, kSleepScreenOptions, 6, 0, 0, 0, nullptr, 0},
    {"sleepScreenSource", "Sleep source", "Display", "enum", 0, kSleepSourceOptions, 3, 0, 0, 0, "sleepScreen", 3},
    {"sleepScreenCoverMode", "Sleep cover mode", "Display", "enum", 0, kFitCropOptions, 2, 0, 0, 0, "sleepScreen", 3},
    {"sleepScreenCoverFilter", "Sleep cover filter", "Display", "enum", 0, kSleepFilterOptions, 3, 0, 0, 0,
     "sleepScreen", 3},
    {"sleepCycleMode", "Sleep cycle mode", "Display", "enum", 0, kCycleOptions, 2, 0, 0, 0, "sleepScreen", 3},
    {"statusBarChapterPageCount", "Chapter page count", "Status bar", "toggle", 1, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"statusBarBookProgressPercentage", "Book progress percentage", "Status bar", "toggle", 1, nullptr, 0, 0, 0, 0,
     nullptr, 0},
    {"statusBarProgressBar", "Progress bar", "Status bar", "enum", 2, kStatusBarOptions, 3, 0, 0, 0, nullptr, 0},
    {"statusBarProgressBarThickness", "Progress bar thickness", "Status bar", "enum", 1, kThicknessOptions, 3, 0, 0, 0,
     nullptr, 0},
    {"statusBarTitle", "Title", "Status bar", "enum", 1, kStatusBarOptions, 3, 0, 0, 0, nullptr, 0},
    {"statusBarBattery", "Battery", "Status bar", "toggle", 1, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"hideBatteryPercentage", "Hide battery", "Display", "enum", 0, kHideBatteryOptions, 3, 0, 0, 0, nullptr, 0},
    {"refreshFrequency", "Refresh frequency", "Display", "enum", 3, kRefreshOptions, 5, 0, 0, 0, nullptr, 0},
    {"uiTheme", "UI theme", "Display", "enum", 3, kThemeOptions, 4, 0, 0, 0, nullptr, 0},
    {"fadingFix", "Sunlight fading fix", "Display", "toggle", 0, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"fontFamily", "Font family", "Reader", "enum", 0, kFontOptions, 3, 0, 0, 0, nullptr, 0},
    {"fontSize", "Font size", "Reader", "enum", 1, kSizeOptions, 4, 0, 0, 0, nullptr, 0},
    {"lineSpacing", "Line spacing", "Reader", "enum", 1, kSpacingOptions, 3, 0, 0, 0, nullptr, 0},
    {"screenMargin", "Screen margin", "Reader", "value", 5, nullptr, 0, 5, 40, 5, nullptr, 0},
    {"paragraphAlignment", "Paragraph alignment", "Reader", "enum", 0, kAlignOptions, 5, 0, 0, 0, nullptr, 0},
    {"embeddedStyle", "Embedded style", "Reader", "toggle", 1, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"hyphenationEnabled", "Hyphenation", "Reader", "toggle", 0, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"orientation", "Orientation", "Reader", "enum", 0, kOrientationOptions, 4, 0, 0, 0, nullptr, 0},
    {"extraParagraphSpacing", "Extra spacing", "Reader", "toggle", 1, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"textAntiAliasing", "Text anti-aliasing", "Reader", "toggle", 1, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"imageRendering", "Images", "Reader", "enum", 0, kImageOptions, 3, 0, 0, 0, nullptr, 0},
    {"sideButtonLayout", "Side button layout", "Controls", "enum", 0, kSideButtonOptions, 2, 0, 0, 0, nullptr, 0},
    {"longPressChapterSkip", "Long press skip", "Controls", "toggle", 1, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"shortPwrBtn", "Short power button", "Controls", "enum", 0, kPowerButtonOptions, 5, 0, 0, 0, nullptr, 0},
    {"sleepTimeout", "Time to sleep", "System", "enum", 2, kSleepTimeoutOptions, 5, 0, 0, 0, nullptr, 0},
    {"showHiddenFiles", "Show hidden files", "System", "toggle", 0, nullptr, 0, 0, 0, 0, nullptr, 0},
    {"backgroundServerMode", "Background server", "System", "enum", 0, kBackgroundServerOptions, 3, 0, 0, 0, nullptr,
     0},
    {"deviceName", "Device name", "System", "string", 0, nullptr, 0, 0, 0, 0, nullptr, 0},
};

JsonDocument loadSnapshot() {
  JsonDocument doc;
  const String saved = Storage.readFile("/settings.json");
  if (!saved.isEmpty()) {
    deserializeJson(doc, saved.c_str());
  }
  if (doc.isNull() || !doc.is<JsonObject>()) {
    const String defaults = network::buildSettingsSnapshotJson(SETTINGS);
    deserializeJson(doc, defaults.c_str());
  }
  return doc;
}

bool saveSnapshot(const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  return Storage.writeFile("/settings.json", body);
}

void appendSetting(JsonArray out, const JsonDocument& snapshot, const HostSetting& setting) {
  JsonObject item = out.add<JsonObject>();
  item["key"] = setting.key;
  item["name"] = setting.name;
  item["category"] = setting.category;
  item["type"] = setting.type;

  if (std::strcmp(setting.type, "string") == 0) {
    item["value"] = snapshot[setting.key] | "";
  } else {
    item["value"] = snapshot[setting.key] | setting.defaultValue;
  }

  if (setting.options != nullptr) {
    JsonArray options = item["options"].to<JsonArray>();
    for (size_t i = 0; i < setting.optionCount; ++i) {
      options.add(setting.options[i]);
    }
  }
  if (std::strcmp(setting.type, "value") == 0) {
    item["min"] = setting.min;
    item["max"] = setting.max;
    item["step"] = setting.step;
  }
  if (setting.visibleKey != nullptr) {
    JsonObject visible = item["visibleWhen"].to<JsonObject>();
    visible["key"] = setting.visibleKey;
    visible["eq"] = setting.visibleEq;
  }
}

void handleGetSettings(HostWebServer& server) {
  const JsonDocument snapshot = loadSnapshot();
  JsonDocument response;
  JsonArray arr = response.to<JsonArray>();
  for (const auto& setting : kSettings) {
    appendSetting(arr, snapshot, setting);
  }

  String body;
  serializeJson(response, body);
  server.send(200, "application/json", body);
}

void handlePostSettings(HostWebServer& server) {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument incoming;
  const auto err = deserializeJson(incoming, server.arg("plain").c_str());
  if (err || !incoming.is<JsonObject>()) {
    server.send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  JsonDocument snapshot = loadSnapshot();
  int applied = 0;
  for (JsonPair kv : incoming.as<JsonObject>()) {
    snapshot[kv.key()] = kv.value();
    ++applied;
  }

  if (!saveSnapshot(snapshot)) {
    server.send(500, "text/plain", "Failed to persist settings");
    return;
  }

  server.send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

void handleGetPlugins(HostWebServer& server) {
  server.send(200, "application/json",
              "{\"web_wifi_setup\":false,\"ota_updates\":false,\"remote_keyboard_input\":false,"
              "\"remote_open_book\":true,\"remote_page_turn\":true,\"user_fonts\":false,"
              "\"todo_planner\":false,\"calibre_sync\":true}");
}

void handleGetOpds(HostWebServer& server) {
  const String saved = Storage.readFile("/opds_servers.json");
  server.send(200, "application/json", saved.isEmpty() ? "[]" : saved);
}

void handlePostOpds(HostWebServer& server) {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  const auto err = deserializeJson(doc, server.arg("plain").c_str());
  if (err || !doc.is<JsonObject>()) {
    server.send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  JsonDocument servers;
  const String saved = Storage.readFile("/opds_servers.json");
  if (!saved.isEmpty()) deserializeJson(servers, saved.c_str());
  JsonArray arr = servers.is<JsonArray>() ? servers.as<JsonArray>() : servers.to<JsonArray>();
  arr.add(doc.as<JsonObject>());

  String body;
  serializeJson(servers, body);
  if (!Storage.writeFile("/opds_servers.json", body)) {
    server.send(500, "text/plain", "Failed to persist OPDS servers");
    return;
  }
  server.send(200, "text/plain", "OK");
}

void handleDeleteOpds(HostWebServer& server) {
  Storage.writeFile("/opds_servers.json", "[]");
  server.send(200, "text/plain", "OK");
}

void handleStatus(HostWebServer& server) {
  server.send(200, "application/json",
              "{\"version\":\"host-shim\",\"protocolVersion\":1,\"wifiStatus\":\"host\",\"ip\":\"127.0.0.1\","
              "\"rssi\":0,\"freeHeap\":0,\"uptime\":0,\"mode\":\"HOST\"}");
}

void appendRecentBook(JsonArray out, JsonObject source) {
  JsonObject book = out.add<JsonObject>();
  book["path"] = source["path"] | "";
  book["title"] = source["title"] | "";
  book["author"] = source["author"] | "";
  book["last_position"] = "";
  book["last_opened"] = 0;
  const char* coverPath = source["coverBmpPath"] | "";
  book["hasCover"] = coverPath[0] != '\0';
  book["progress"] = nullptr;
}

void handleRecentBooks(HostWebServer& server) {
  JsonDocument response;
  JsonArray out = response.to<JsonArray>();

  const String saved = Storage.readFile("/.crosspoint/recent.json");
  if (!saved.isEmpty()) {
    JsonDocument doc;
    const auto err = deserializeJson(doc, saved.c_str());
    if (err) {
      server.send(500, "text/plain", String("Invalid recent books JSON: ") + err.c_str());
      return;
    }

    JsonArray books = doc["books"].as<JsonArray>();
    for (JsonObject book : books) {
      appendRecentBook(out, book);
    }
  }

  String body;
  serializeJson(response, body);
  server.send(200, "application/json", body);
}

}  // namespace

void mountStubApiRoutes(HostWebServer& server, const std::string&) {
  server.on("/api/settings", HTTP_GET, [&server] { handleGetSettings(server); });
  server.on("/api/settings", HTTP_POST, [&server] { handlePostSettings(server); });
  server.on("/api/plugins", HTTP_GET, [&server] { handleGetPlugins(server); });
  server.on("/api/opds", HTTP_GET, [&server] { handleGetOpds(server); });
  server.on("/api/opds", HTTP_POST, [&server] { handlePostOpds(server); });
  server.on("/api/opds/delete", HTTP_POST, [&server] { handleDeleteOpds(server); });
  server.on("/api/status", HTTP_GET, [&server] { handleStatus(server); });
  server.on("/api/recent", HTTP_GET, [&server] { handleRecentBooks(server); });
}

}  // namespace host
