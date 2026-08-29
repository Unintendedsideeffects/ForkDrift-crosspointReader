#include "network/server/CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <FeatureFlags.h>
#include <FsHelpers.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <Logging.h>
#include <Memory.h>
#include <OpdsParser.h>
#include <OpdsStream.h>
#include <WiFi.h>
#ifndef SIMULATOR
#include <ESPmDNS.h>
#endif
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <new>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "SpiBusMutex.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "core/features/FeatureCatalog.h"
#include "core/features/FeatureModules.h"
#include "core/features/KoreaderOpdsBridge.h"
#include "network/html/FilesPageHtml.generated.h"
#include "network/html/FontsPageHtml.generated.h"
#include "network/html/HomePageHtml.generated.h"
#include "network/html/SettingsPageHtml.generated.h"
#include "network/html/js/jszip_minJs.generated.h"
#include "network/http/BufferedHttpUpload.h"
#include "network/http/HttpDownloader.h"
#include "network/server/NotesApi.h"
#include "network/server/RecentBookJson.h"
#include "network/server/SleepCoverApi.h"
#include "network/server/TodoPlannerApi.h"
#include "network/server/WebDAVHandler.h"
#include "network/server/WebRouteTableHandler.h"
#if ENABLE_REMOTE_CONTROL
#include "network/server/RemoteControlApi.h"
#endif
#include "network/server/WebUtils.h"
#include "util/BookProgressDataStore.h"
#include "util/DateUtils.h"
#include "util/InputValidation.h"
#include "util/MaintenanceUtils.h"
#include "util/NetworkNames.h"
#include "util/PathUtils.h"
#include "util/RecentBooksStore.h"
#include "util/WifiCredentialStore.h"
#if ENABLE_WIFI_CLOCK
#include "util/TimeSync.h"
#endif

namespace {
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint8_t CROSSPOINT_PROTOCOL_VERSION = 1;
constexpr uint16_t LOCAL_UDP_PORT = 8134;
constexpr uint32_t WEB_SERVER_MIN_SAFE_HEAP_BYTES = 12 * 1024;
constexpr uint32_t WS_STARTUP_BYTES = 4096;
// MDNS.begin() spins up the mDNS service task (4 KB stack) plus the server struct
// and its packet buffers. The task stack is the single largest contiguous request,
// so it gets the same 4 KB contiguous floor as the WebSocket server. Confirm the
// total against the "[MEM] Free heap before/after mDNS" pair in ensureMdns().
constexpr uint32_t MDNS_STARTUP_BYTES = 6144;
constexpr uint32_t kMinHeapForOpdsMutation = 16000;
constexpr uint32_t kMinHeapForOpdsTest = HttpDownloader::MIN_HEAP_FOR_HTTPS + 8000;
constexpr size_t WS_CONTROL_MESSAGE_MAX_BYTES = 1024;
constexpr size_t WS_UPLOAD_MAX_BYTES = 512UL * 1024UL * 1024UL;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

// Helper function to clear epub cache after upload
void clearEpubCacheIfNeeded(const String& filePath) {
  // Only clear cache for .epub files
  if (FsHelpers::hasEpubExtension(filePath)) {
    // Re-uploading a book must not cost the reader their place in it, so drop
    // only the derived layout and keep progress/annotations.
    Epub(filePath.c_str(), "/.crosspoint").clearRenderCache();
    LOG_DBG("WEB", "Cleared epub render cache for: %s", filePath.c_str());
  }
}

// Helper to invalidate sleep image cache when /sleep is modified.
void invalidateSleepCacheIfNeeded(const String& filePath) {
  String lowerPath = filePath;
  lowerPath.toLowerCase();
  if (lowerPath.startsWith("/sleep/") || lowerPath.equals("/sleep")) {
    invalidateSleepImageCache();
  }
}

void invalidateFeatureCachesIfNeeded(const String& filePath) {
  core::FeatureModules::onWebFileChanged(filePath);
  invalidateSleepCacheIfNeeded(filePath);
}

network::BufferedHttpUploadSession& httpUploadSession() { return network::sharedBufferedHttpUploadSession(); }

bool resolveWebUploadTarget(WebServer* server, const char* uploadFileName, char* uploadPath, size_t uploadPathSize,
                            char* filePath, size_t filePathSize, char* error, size_t errorSize) {
  if (server == nullptr) {
    snprintf(error, errorSize, "Upload server unavailable");
    return false;
  }

  if (!PathUtils::isValidFilename(uploadFileName)) {
    snprintf(error, errorSize, "Invalid filename");
    LOG_WRN("WEB", "[UPLOAD] Invalid filename rejected: %s", uploadFileName);
    return false;
  }
  if (PathUtils::isProtectedWebComponent(uploadFileName)) {
    snprintf(error, errorSize, "Cannot upload protected files");
    LOG_WRN("WEB", "[UPLOAD] Protected filename rejected: %s", uploadFileName);
    return false;
  }

  uploadPath[0] = '/';
  uploadPath[1] = '\0';
  if (server->hasArg("path")) {
    if (!PathUtils::urlDecode(server->arg("path").c_str(), uploadPath, uploadPathSize)) {
      snprintf(error, errorSize, "Path too long");
      LOG_WRN("WEB", "[UPLOAD] Path decode exceeded %u bytes", static_cast<unsigned int>(uploadPathSize));
      return false;
    }

    if (!PathUtils::isValidSdPath(uploadPath)) {
      snprintf(error, errorSize, "Invalid path");
      LOG_WRN("WEB", "[UPLOAD] Path validation failed: %s", uploadPath);
      return false;
    }

    if (!PathUtils::normalizePathInPlace(uploadPath, uploadPathSize)) {
      snprintf(error, errorSize, "Path too long");
      LOG_WRN("WEB", "[UPLOAD] Path normalization exceeded %u bytes", static_cast<unsigned int>(uploadPathSize));
      return false;
    }

    if (PathUtils::pathContainsProtectedItem(uploadPath)) {
      snprintf(error, errorSize, "Cannot upload to protected path");
      LOG_WRN("WEB", "[UPLOAD] Protected upload path rejected: %s", uploadPath);
      return false;
    }
  }

  const size_t uploadPathLength = std::strlen(uploadPath);
  const bool endsWithSlash = uploadPathLength > 0 && uploadPath[uploadPathLength - 1] == '/';
  const int written = snprintf(filePath, filePathSize, "%s%s%s", uploadPath, endsWithSlash ? "" : "/", uploadFileName);
  if (written < 0 || static_cast<size_t>(written) >= filePathSize) {
    snprintf(error, errorSize, "Path too long");
    LOG_WRN("WEB", "[UPLOAD] Combined upload path exceeds limit (%d chars)", written);
    return false;
  }
  if (!PathUtils::isValidSdPath(filePath)) {
    snprintf(error, errorSize, "Path too long");
    LOG_WRN("WEB", "[UPLOAD] Combined upload path rejected: %s", filePath);
    return false;
  }
  return true;
}

const network::BufferedHttpUploadConfig kWebUploadConfig = {"UPLOAD",
                                                            "WEB",
                                                            "Failed to create file on SD card",
                                                            "Failed to write to SD card - disk may be full",
                                                            "Failed to write final data to SD card",
                                                            "Upload aborted",
                                                            true,
                                                            resolveWebUploadTarget};

}  // namespace

CrossPointWebServer::CrossPointWebServer() {}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::setApRedirectPath(std::string path) {
  if (path.empty()) {
    apRedirectPath = "/";
    return;
  }
  if (path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  apRedirectPath = std::move(path);
}

void CrossPointWebServer::noteWebUiAccess() const {
#if ENABLE_WIFI_CLOCK
  if (!apMode && WiFi.status() == WL_CONNECTED) {
    TimeSync::noteWebUiAccess(true);
  }
#endif
}

void CrossPointWebServer::begin(const ServerRole role) {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  serverRole = role;

  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new (std::nothrow) WebServer(port));

  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes (largest=%d)", ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  LOG_DBG("WEB", "Setting up routes...");
  mountRoutes();
  const uint32_t freeHeapAfterRouteSetup = ESP.getFreeHeap();
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes (largest=%d)", freeHeapAfterRouteSetup,
          ESP.getMaxAllocHeap());

  if (freeHeapAfterRouteSetup < WEB_SERVER_MIN_SAFE_HEAP_BYTES) {
    LOG_ERR("WEB", "Aborting server startup: only %u bytes free after route setup", freeHeapAfterRouteSetup);
    server.reset();
    return;
  }

#if CROSSPOINT_HAS_NETWORKUDP
  const char* davHeaders[] = {"Depth", "Destination", "Overwrite", "If", "Lock-Token", "Timeout"};
  server->collectHeaders(davHeaders, 6);
  auto* davHandler = new (std::nothrow) WebDAVHandler();
  if (davHandler) {
    server->addHandler(davHandler);
    LOG_DBG("WEB", "WebDAV handler initialized");
  } else {
    LOG_ERR("WEB", "OOM: WebDAVHandler; WebDAV disabled");
  }
  LOG_DBG("WEB", "[MEM] Free heap after WebDAV: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif

  server->begin();
  LOG_DBG("WEB", "[MEM] Free heap after listen: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (serverRole == ServerRole::Foreground) {
    if (!ensureWs()) {
      server->stop();
      server.reset();
      return;
    }
  }

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);
  LOG_DBG("WEB", "[MEM] Free heap after UDP: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  const uint32_t freeHeapAfterStartup = ESP.getFreeHeap();
  if (freeHeapAfterStartup < WEB_SERVER_MIN_SAFE_HEAP_BYTES) {
    LOG_ERR("WEB", "Aborting server startup: only %u bytes free after startup", freeHeapAfterStartup);
    if (wsServer) {
      wsServer->close();
      wsServer.reset();
      wsInstance = nullptr;
    }
    if (udpActive) {
      udp.stop();
      udpActive = false;
    }
    server->stop();
    server.reset();
    return;
  }

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  if (wsServer) {
    LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  }
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes (largest=%d)", freeHeapAfterStartup,
          ESP.getMaxAllocHeap());
}

bool CrossPointWebServer::ensureWs() {
  if (wsServer) {
    return true;
  }
  if (!heapguard::canAllocate(WS_STARTUP_BYTES, 4096)) {
    LOG_ERR("WEB", "WS skipped: heap free=%u largest=%u need=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(WS_STARTUP_BYTES));
    return false;
  }
  LOG_DBG("WEB", "[MEM] Free heap before WebSocket: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  wsServer.reset(new (std::nothrow) WebSocketsServer(wsPort));
  if (!wsServer) {
    LOG_ERR("WEB", "Failed to create WebSocket server");
    return false;
  }
  wsInstance = this;
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");
  LOG_DBG("WEB", "[MEM] Free heap after WebSocket: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

void CrossPointWebServer::ensureMdns() {
  if (mdnsStarted) {
    return;
  }
#ifndef SIMULATOR
  // Same admission rule as ensureWs(). Discovery is a convenience; a UDP "hello"
  // arriving while the heap is tight must not turn advertisement into the
  // fragmentation event it was meant to avoid.
  if (!heapguard::canAllocate(MDNS_STARTUP_BYTES, 4096)) {
    LOG_ERR("WEB", "mDNS skipped: heap free=%u largest=%u need=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(MDNS_STARTUP_BYTES));
    return;
  }
  LOG_DBG("WEB", "[MEM] Free heap before mDNS: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  char hostname[40];
  NetworkNames::getDeviceHostname(hostname, sizeof(hostname));
  if (MDNS.begin(hostname)) {
    mdnsStarted = true;
    LOG_INF("WEB", "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_ERR("WEB", "mDNS failed to start");
  }
  LOG_DBG("WEB", "[MEM] Free heap after mDNS: %d bytes (largest=%d)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
}

void CrossPointWebServer::mountRoutes() {
  if (!server) {
    return;
  }

  auto* table = new (std::nothrow) WebRouteTableHandler(this);
  if (!table) {
    LOG_ERR("WEB", "OOM: WebRouteTableHandler");
    return;
  }
  server->addHandler(table);
  server->onNotFound([this] { handleNotFound(); });
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  if (mdnsStarted) {
#ifndef SIMULATOR
    MDNS.end();
#endif
    mdnsStarted = false;
  }

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }
  wsUploadOwnerValid = false;
  wsUploadOwnerClient = 0;

  // Close any in-progress HTTP upload
  httpUploadSession().reset();

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[32];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          bool wsReady = true;
          if (serverRole == ServerRole::Background) {
            wsReady = ensureWs();
            ensureMdns();
          }
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ")";
          if (wsReady && wsServer) {
            message += ";" + String(wsPort);
          }
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static_assert(HomePageHtmlCompressedSize == sizeof(HomePageHtml), "Home page compressed size mismatch");
static_assert(FilesPageHtmlCompressedSize == sizeof(FilesPageHtml), "Files page compressed size mismatch");
static_assert(SettingsPageHtmlCompressedSize == sizeof(SettingsPageHtml), "Settings page compressed size mismatch");

static bool parseStrictSize(const String& token, size_t& outValue) {
  return InputValidation::parseStrictPositiveSize(token.c_str(), token.length(), WS_UPLOAD_MAX_BYTES, outValue);
}

#if ENABLE_WIFI_CLOCK
void CrossPointWebServer::handleSetTime() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  if (doc["epoch"].isNull()) {
    server->send(400, "text/plain", "Missing epoch");
    return;
  }
  constexpr std::time_t kMinValidTime = 1577836800;  // 2020-01-01 UTC
  const auto epoch = static_cast<std::time_t>(doc["epoch"].as<uint32_t>());
  if (epoch < kMinValidTime) {
    server->send(400, "text/plain", "epoch out of range");
    return;
  }
  TimeSync::setManualTime(epoch);
  server->send(200, "application/json", "{\"ok\":true}");
}
#endif

void CrossPointWebServer::handleTodoEntry() {
  const bool plannerEnabled = core::FeatureCatalog::isEnabled("todo_planner");
  const bool markdownEnabled = core::FeatureCatalog::isEnabled("markdown");
  const std::string today = DateUtils::currentDate();
  const network::TodoPlannerHttpResult result = network::handleTodoEntryRequest(
      plannerEnabled, markdownEnabled, server->hasArg("text") ? server->arg("text") : String(), server->arg("type"),
      today);
  if (result.ok() && !result.targetPath.empty()) {
    invalidateFeatureCachesIfNeeded(String(result.targetPath.c_str()));
  }
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleTodoTodayGet() const {
  const bool plannerEnabled = core::FeatureCatalog::isEnabled("todo_planner");
  const bool markdownEnabled = core::FeatureCatalog::isEnabled("markdown");
  const std::string today = DateUtils::currentDate();
  const network::TodoPlannerHttpResult result =
      network::handleTodoTodayGetRequest(plannerEnabled, markdownEnabled, today);
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleTodoTodaySave() const {
  const bool plannerEnabled = core::FeatureCatalog::isEnabled("todo_planner");
  const bool markdownEnabled = core::FeatureCatalog::isEnabled("markdown");
  const std::string today = DateUtils::currentDate();
  const network::TodoPlannerHttpResult result =
      network::handleTodoTodaySaveRequest(plannerEnabled, markdownEnabled, server->hasArg("plain"),
                                          server->hasArg("plain") ? server->arg("plain") : String(), today);
  if (result.ok() && !result.targetPath.empty()) {
    invalidateFeatureCachesIfNeeded(String(result.targetPath.c_str()));
  }
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleNotesEntry() const {
  const network::NotesHttpResult result =
      network::handleNotesEntryRequest(core::FeatureCatalog::isEnabled("notes"), server->arg("text"));
  if (result.ok()) {
    requestCount++;
    invalidateFeatureCachesIfNeeded("/notes.txt");
  }
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleNotesGet() const {
  const network::NotesHttpResult result = network::handleNotesGetRequest(core::FeatureCatalog::isEnabled("notes"));
  if (result.ok()) {
    requestCount++;
  }
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleNotesSave() const {
  const network::NotesHttpResult result = network::handleNotesSaveRequest(
      core::FeatureCatalog::isEnabled("notes"), server->hasArg("plain"), server->arg("plain"));
  if (result.ok()) {
    requestCount++;
    invalidateFeatureCachesIfNeeded("/notes.txt");
  }
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root;
  {
    SpiBusMutex::Guard guard;
    root = Storage.open(path);
  }

  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  while (true) {
    FileInfo info;
    bool shouldHide = false;

    // Scope SD card operations with mutex
    {
      SpiBusMutex::Guard guard;
      HalFile file = root.openNextFile();
      if (!file) {
        break;
      }

      char name[500];
      file.getName(name, sizeof(name));
      auto fileName = String(name);

      shouldHide =
          (!SETTINGS.showHiddenFiles && fileName.startsWith(".")) || PathUtils::isProtectedWebComponent(fileName);

      if (!shouldHide) {
        info.name = fileName;
        info.isDirectory = file.isDirectory();

        if (info.isDirectory) {
          info.size = 0;
          info.isEpub = false;
        } else {
          info.size = file.size();
          info.isEpub = isEpubFile(info.name);
        }
      }
      file.close();
    }

    // Callback performs network operations - run without mutex
    if (!shouldHide) {
      callback(info);
    }

    yield();               // Yield to allow WiFi and other tasks to process during long scans
    esp_task_wdt_reset();  // Reset watchdog to prevent timeout on large directories
  }

  {
    SpiBusMutex::Guard guard;
    root.close();
  }
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleUpload() {
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  httpUploadSession().handleUpload(server.get(), kWebUploadConfig);
}

void CrossPointWebServer::handleUploadPost() {
  requestCount++;
  if (httpUploadSession().succeeded()) {
    invalidateFeatureCachesIfNeeded(httpUploadSession().filePath());
    core::FeatureModules::onUploadCompleted(httpUploadSession().uploadPath(), httpUploadSession().fileName());
    server->send(200, "text/plain", String("File uploaded successfully: ") + httpUploadSession().fileName());
  } else {
    const char* uploadError = httpUploadSession().error();
    server->send(400, "text/plain", uploadError[0] == '\0' ? "Unknown error during upload" : uploadError);
  }
}

void CrossPointWebServer::handleRecentBooks() const {
  const auto books = RECENT_BOOKS.getBooksSnapshot();
  const bool includePokemon = core::FeatureCatalog::isEnabled("pokemon_party");

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  bool seenFirst = false;
  for (const auto& book : books) {
    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }

    server->sendContent(network::buildRecentBookJson(book, includePokemon));
  }

  server->sendContent("]");
  server->sendContent("");
}

void CrossPointWebServer::handleGetBookProgress() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String bookPath = PathUtils::urlDecode(server->arg("path"));
  if (!PathUtils::isValidSdPath(bookPath)) {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  bookPath = PathUtils::normalizePath(bookPath);
  if (PathUtils::pathContainsProtectedItem(bookPath)) {
    server->send(403, "text/plain", "Cannot access protected items");
    return;
  }

  if (!Storage.exists(bookPath.c_str())) {
    server->send(404, "text/plain", "Book not found");
    return;
  }

  if (!BookProgressDataStore::supportsBookPath(bookPath.c_str())) {
    server->send(400, "text/plain", "Unsupported book type");
    return;
  }

  JsonDocument response;
  response["path"] = bookPath;

  BookProgressDataStore::ProgressData progress;
  if (BookProgressDataStore::loadProgress(bookPath.c_str(), progress)) {
    network::appendBookProgressJson(response["progress"].to<JsonObject>(), progress);
  } else {
    response["progress"] = nullptr;
  }

  String json;
  serializeJson(response, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleSleepCoverGet() const {
  const auto result = network::buildSleepCoverGetResponse(SETTINGS.sleepPinnedPath);
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleSleepCoverPin() {
  const auto result = network::handleSleepCoverPinRequest(
      server->hasArg("plain"), server->hasArg("plain") ? server->arg("plain") : String(), SETTINGS.sleepPinnedPath,
      sizeof(SETTINGS.sleepPinnedPath),
      [](const String& bookPath, std::string& coverPath) {
        const auto books = RECENT_BOOKS.getBooksSnapshot();
        for (const auto& book : books) {
          if (book.path == bookPath.c_str()) {
            coverPath = book.coverBmpPath;
            return true;
          }
        }
        return core::FeatureModules::tryGetDocumentCoverPath(bookPath, coverPath);
      },
      [] {
        if (SETTINGS.sleepPinnedPath[0] != '\0') {
          SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
        }
        SpiBusMutex::Guard guard;
        return SETTINGS.saveToFile();
      });

  server->send(result.statusCode, result.contentType, result.body);
}

#if ENABLE_REMOTE_CONTROL
void CrossPointWebServer::handleOpenBook() {
  const auto result = network::parseOpenBookHttpRequest(server->hasArg("plain"), server->arg("plain"));
  if (result.statusCode == 202) {
    APP_STATE.setPendingOpenPath(result.path);
  }
  server->send(result.statusCode, result.contentType, result.body);
}

void CrossPointWebServer::handleRemoteButton() {
  const auto result = network::parseRemoteButtonHttpRequest(server->hasArg("plain"), server->arg("plain"));
  if (result.statusCode == 202) {
    APP_STATE.setPendingPageTurn(result.pageTurn);
  }
  server->send(result.statusCode, result.contentType, result.body);
}
#endif  // ENABLE_REMOTE_CONTROL

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    // Never expose passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForOpdsMutation) {
    LOG_WRN("WEB", "OPDS POST rejected: %u bytes free < %u required (%u short)", freeHeap, kMinHeapForOpdsMutation,
            kMinHeapForOpdsMutation - freeHeap);
    server->send(503, "application/json", "{\"error\":\"low memory\"}");
    return;
  }

  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password — the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    // Preserve existing password if not explicitly provided
    if (!hasPasswordField) {
      const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
      if (existing) password = existing->password;
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

void CrossPointWebServer::handleTestOpdsServer() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForOpdsTest) {
    LOG_WRN("WEB", "OPDS test rejected: %u bytes free < %u required (%u short)", freeHeap, kMinHeapForOpdsTest,
            kMinHeapForOpdsTest - freeHeap);
    server->send(503, "application/json", "{\"error\":\"low memory\"}");
    return;
  }

  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain")) != DeserializationError::Ok) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  const std::string url = doc["url"] | std::string("");
  const std::string username = doc["username"] | std::string("");
  const std::string password = doc["password"] | std::string("");

  if (url.empty()) {
    server->send(400, "text/plain", "URL required");
    return;
  }

  JsonDocument resp;
  OpdsParser parser;
  bool fetched = false;
  {
    OpdsParserStream stream{parser};
    fetched = HttpDownloader::fetchUrl(url, stream, username, password);
  }
  if (fetched && parser) {
    resp["ok"] = true;
    resp["status"] = 200;
    resp["entries"] = parser.getEntries().size();
    resp["error"] = nullptr;
    String json;
    serializeJson(resp, json);
    server->send(200, "application/json", json);
    return;
  }

  // A failed streamed fetch does not expose its HTTP status. Probe once more so
  // the UI can distinguish bad credentials/URLs from a malformed OPDS document.
  const int code = HttpDownloader::probeUrl(url, username, password);
  resp["status"] = code;
  resp["ok"] = false;
  if (code > 0) {
    if (code == 401)
      resp["error"] = "Unauthorized — check credentials";
    else if (code == 403)
      resp["error"] = "Forbidden";
    else if (code == 404)
      resp["error"] = "Not found — check URL";
    else if (code >= 200 && code < 300)
      resp["error"] = "Response is not a valid OPDS feed";
    else
      resp["error"] = "Server returned " + std::to_string(code);
  } else {
    // Negative codes are HTTPClient error constants
    if (code == HTTPC_ERROR_CONNECTION_REFUSED)
      resp["error"] = "Connection refused";
    else if (code == HTTPC_ERROR_SEND_HEADER_FAILED || code == HTTPC_ERROR_SEND_PAYLOAD_FAILED)
      resp["error"] = "Send failed";
    else if (code == HTTPC_ERROR_NOT_CONNECTED || code == HTTPC_ERROR_NO_HTTP_SERVER)
      resp["error"] = "Not connected";
    else
      resp["error"] = "Connection failed";
  }

  String json;
  serializeJson(resp, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleKoreaderUseOpds() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < kMinHeapForOpdsMutation) {
    LOG_WRN("WEB", "KOReader-use-OPDS rejected: %u bytes free < %u required (%u short)", freeHeap,
            kMinHeapForOpdsMutation, kMinHeapForOpdsMutation - freeHeap);
    server->send(503, "application/json", "{\"error\":\"low memory\"}");
    return;
  }

  if (!core::FeatureModules::hasCapability(core::Capability::KoreaderSync)) {
    server->send(400, "text/plain", "KOReader sync not available in this build");
    return;
  }
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain")) != DeserializationError::Ok) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }
  const int index = doc["index"] | -1;
  if (index < 0) {
    server->send(400, "text/plain", "index required");
    return;
  }
  const OpdsServer* srv = OPDS_STORE.getServer(static_cast<size_t>(index));
  if (srv == nullptr) {
    server->send(404, "text/plain", "OPDS server not found");
    return;
  }
  const std::string syncUrl = core::applyOpdsServerToKoreaderSync(*srv);
  if (syncUrl.empty()) {
    server->send(400, "text/plain", "Server URL has no usable host");
    return;
  }
  JsonDocument resp;
  resp["ok"] = true;
  resp["serverUrl"] = syncUrl;
  resp["username"] = srv->username;  // password is never returned
  String json;
  serializeJson(resp, json);
  server->send(200, "application/json", json);
  LOG_DBG("WEB", "KOReader sync configured from OPDS server %d", index);
}

// ---- Wi-Fi Credentials API ----

void CrossPointWebServer::handleGetWifiNetworks() const {
  const auto& credentials = WIFI_STORE.getCredentials();
  const std::string& lastConnectedSsid = WIFI_STORE.getLastConnectedSsid();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !credentials[i].password.empty();
    doc["isLastConnected"] = credentials[i].ssid == lastConnectedSsid;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void CrossPointWebServer::handlePostWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", "SSID is required");
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    const auto& credentials = WIFI_STORE.getCredentials();
    if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }

    const std::string oldSsid = credentials[static_cast<size_t>(idx)].ssid;
    if (!hasPasswordField) {
      password = credentials[static_cast<size_t>(idx)].password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", "Failed to update Wi-Fi network");
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", "Cannot add network (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  const auto& credentials = WIFI_STORE.getCredentials();
  if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }

  const std::string ssid = credentials[static_cast<size_t>(idx)].ssid;
  if (!WIFI_STORE.removeCredential(ssid)) {
    server->send(400, "text/plain", "Failed to delete Wi-Fi network");
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  server->send(200, "text/plain", "OK");
}

void CrossPointWebServer::handleForgetAllWifiNetworks() {
  MaintenanceUtils::clearWifiNetworks();
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Saved WiFi networks cleared";
  String output;
  serializeJson(doc, output);
  server->send(200, "application/json", output);
}

void CrossPointWebServer::handleMaintenanceValidateSleepImages() {
  const MaintenanceUtils::SleepValidationResult result = MaintenanceUtils::validateSleepImages();
  JsonDocument doc;
  doc["valid"] = result.valid;
  doc["invalid"] = result.invalid;
  if (result.valid == 0 && result.invalid == 0) {
    doc["message"] = "No sleep images found";
  } else if (result.invalid == 0 && result.valid > 0) {
    doc["message"] = "All validated";
  } else {
    char message[48];
    snprintf(message, sizeof(message), "%d valid, %d invalid", result.valid, result.invalid);
    doc["message"] = message;
  }
  String output;
  serializeJson(doc, output);
  server->send(200, "application/json", output);
}

void CrossPointWebServer::handleMaintenanceClearCache() {
  const MaintenanceUtils::CacheClearResult result = MaintenanceUtils::clearReadingCache();
  JsonDocument doc;
  doc["removed"] = result.removed;
  doc["failed"] = result.failed;
  char message[64];
  snprintf(message, sizeof(message), "Removed %d, %d failed", result.removed, result.failed);
  doc["message"] = message;
  String output;
  serializeJson(doc, output);
  server->send(200, "application/json", output);
}

void CrossPointWebServer::handleMaintenanceResetSettings() {
  if (!MaintenanceUtils::resetSettingsToDefaults()) {
    server->send(500, "application/json", "{\"ok\":false,\"message\":\"Failed to reset settings\"}");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true,\"message\":\"Settings reset to defaults\"}");
}

void CrossPointWebServer::handleMaintenanceClearLogs() {
  MaintenanceUtils::clearLogs();
  server->send(200, "application/json", "{\"ok\":true,\"message\":\"Logs cleared\"}");
}

void CrossPointWebServer::handleMaintenanceClearCrashes() {
  MaintenanceUtils::clearCrashReports();
  server->send(200, "application/json", "{\"ok\":true,\"message\":\"Crash reports cleared\"}");
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete

// --- Font management handlers ---

void CrossPointWebServer::handleFontsPage() const {
  sendPrecompressedHtml(server.get(), FontsPageHtml, FontsPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() const {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;

  for (const auto& family : families) {
    JsonObject fObj = arr.add<JsonObject>();
    fObj["name"] = family.name;

    JsonArray sizes = fObj["sizes"].to<JsonArray>();
    for (uint8_t s : family.availableSizes()) {
      sizes.add(s);
    }

    JsonArray files = fObj["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      JsonObject fileObj = files.add<JsonObject>();
      // Extract filename from full path
      const char* name = strrchr(file.path.c_str(), '/');
      fileObj["name"] = name ? name + 1 : file.path.c_str();

      // Stat the file for size
      HalFile f;
      if (Storage.openFileForRead("WEB", file.path.c_str(), f)) {
        fileObj["size"] = static_cast<unsigned long>(f.size());
        f.close();
      } else {
        fileObj["size"] = 0;
      }
    }
  }

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      esp_task_wdt_reset();
      String family = server->arg("family");
      // Reset all upload state up-front: a previous aborted upload can leave a
      // stale open handle/path that the invalid-filename early-breaks below
      // would otherwise reuse and crash on (upstream #2253).
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.magicHeaderPos = 0;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;
      fontUpload.buffer.reset();

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.crosspoint/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }

      fontUpload.familyName = family.c_str();

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      char path[128];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;

      {
        SpiBusMutex::Guard guard;
        if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
          LOG_ERR("WEB", "Failed to open font file for write: %s", path);
          break;
        }
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      esp_task_wdt_reset();

      if (!fontUpload.buffer) {
        fontUpload.buffer = makeUniqueNoThrow<uint8_t[]>(FontUploadState::BUFFER_SIZE);
        if (!fontUpload.buffer) {
          LOG_ERR("WEB", "OOM: font upload buffer");
          fontUpload.valid = false;
          break;
        }
      }

      // Validate magic bytes once the first 8 file bytes have accumulated.
      // Accumulating (rather than checking only when the first chunk is >= 8
      // bytes) closes a bypass: a 1-7 byte first chunk would otherwise skip the
      // check permanently and let an arbitrary blob be written under a .cpfont
      // name. Cleanup of the partial file is handled by UPLOAD_FILE_END when
      // valid == false, matching the existing error path.
      if (!fontUpload.magicChecked) {
        const size_t need = sizeof(fontUpload.magicHeader) - fontUpload.magicHeaderPos;
        const size_t take = (upload.currentSize < need) ? upload.currentSize : need;
        memcpy(fontUpload.magicHeader + fontUpload.magicHeaderPos, upload.buf, take);
        fontUpload.magicHeaderPos += take;
        if (fontUpload.magicHeaderPos >= sizeof(fontUpload.magicHeader)) {
          if (memcmp(fontUpload.magicHeader, "CPFONT\0\0", 8) != 0) {
            LOG_ERR("WEB", "Invalid .cpfont magic bytes");
            fontUpload.valid = false;
            break;
          }
          fontUpload.magicChecked = true;
        }
      }

      // Buffer writes for efficiency
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.get() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          size_t written = 0;
          {
            SpiBusMutex::Guard guard;
            written = fontUpload.file.write(fontUpload.buffer.get(), fontUpload.bufferPos);
          }
          if (written != fontUpload.bufferPos) {
            LOG_ERR("WEB", "Font write failed (SD full?)");
            fontUpload.valid = false;
            fontUpload.bufferPos = 0;
            break;
          }
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          esp_task_wdt_reset();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0 && fontUpload.buffer) {
        size_t written = 0;
        {
          SpiBusMutex::Guard guard;
          written = fontUpload.file.write(fontUpload.buffer.get(), fontUpload.bufferPos);
        }
        if (written != fontUpload.bufferPos) {
          LOG_ERR("WEB", "Font write failed on final flush (SD full?)");
          fontUpload.valid = false;
        } else {
          fontUpload.bytesWritten += fontUpload.bufferPos;
        }
        fontUpload.bufferPos = 0;
      }
      {
        SpiBusMutex::Guard guard;
        fontUpload.file.close();
      }

      // A file shorter than the 8-byte magic header can never be a valid
      // .cpfont — reject it so the magic check can't be skipped by truncation.
      if (fontUpload.valid && !fontUpload.magicChecked) {
        LOG_ERR("WEB", "Font upload too small to validate magic bytes");
        fontUpload.valid = false;
      }

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        SpiBusMutex::Guard guard;
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      fontUpload.buffer.reset();
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      {
        SpiBusMutex::Guard guard;
        fontUpload.file.close();
      }
      if (!fontUpload.filePath.empty()) {
        SpiBusMutex::Guard guard;
        Storage.remove(fontUpload.filePath.c_str());
      }
      fontUpload.valid = false;
      fontUpload.buffer.reset();
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid .cpfont file\"}");
  }
}

void CrossPointWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}
