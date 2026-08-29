#pragma once

#include <FeatureFlags.h>
#include <HalStorage.h>
#include <WebServer.h>

#if __has_include(<WebSocketsServer.h>)
#include <WebSocketsServer.h>
#else
class WebSocketsServer;
// Minimal WStype_t definition for builds without WebSocketsServer.h
enum WStype_t : unsigned char {
  WStype_ERROR = 0,
  WStype_DISCONNECTED,
  WStype_CONNECTED,
  WStype_TEXT,
  WStype_BIN,
  WStype_FRAGMENT_TEXT_START,
  WStype_FRAGMENT_BIN_START,
  WStype_FRAGMENT,
  WStype_FRAGMENT_FIN,
  WStype_PING,
  WStype_PONG
};
#endif

#if __has_include(<NetworkUdp.h>)
#include <NetworkUdp.h>
#define CROSSPOINT_HAS_NETWORKUDP 1
using CrossPointUdpType = NetworkUDP;
#else
#include <WiFiUdp.h>
#define CROSSPOINT_HAS_NETWORKUDP 0
using CrossPointUdpType = WiFiUDP;
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class WebRouteTableHandler;

struct FileInfo {
  String name;
  size_t size = 0;
  bool isEpub = false;
  bool isDirectory = false;
};

class CrossPointWebServer {
  friend class WebRouteTableHandler;

 public:
  enum class ServerRole : uint8_t { Foreground, Background };

  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  CrossPointWebServer();
  ~CrossPointWebServer();

  void begin(ServerRole role = ServerRole::Foreground);

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

  // Count of meaningful API requests (status checks, uploads, downloads)
  uint32_t getRequestCount() const { return requestCount; }

  // Returns true if any push/pull API request was received since the server started
  bool hadApiActivity() const { return requestCount > 0; }

  // Override the AP-mode captive-portal redirect target. Defaults to "/".
  void setApRedirectPath(std::string path);

 private:
  void noteWebUiAccess() const;
  void mountRoutes();
  void ensureMdns();
  bool ensureWs();

  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;
  bool mdnsStarted = false;
  ServerRole serverRole = ServerRole::Foreground;
  std::string apRedirectPath = "/";
  uint16_t port = 80;
  uint16_t wsPort = 81;               // WebSocket port
  mutable uint32_t requestCount = 0;  // Incremented on status/upload/download
  CrossPointUdpType udp;
  bool udpActive = false;

  // Sentinel value meaning "no WebSocket client currently owns an upload slot".
  static constexpr uint8_t kNoUploadClient = 255;

  // WebSocket upload state
  HalFile wsUploadFile;
  String wsUploadFileName;
  String wsUploadPath;
  size_t wsUploadSize = 0;
  size_t wsUploadReceived = 0;
  size_t wsLastProgressSent = 0;
  unsigned long wsUploadStartTime = 0;
  bool wsUploadInProgress = false;
  uint8_t wsUploadClientNum = kNoUploadClient;
  uint8_t wsUploadOwnerClient = 0;
  bool wsUploadOwnerValid = false;
  String wsLastCompleteName;
  size_t wsLastCompleteSize = 0;
  unsigned long wsLastCompleteAt = 0;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handlePlugins() const;
  void handleTodoEntry();
  void handleTodoTodayGet() const;
  void handleTodoTodaySave() const;
  void handleNotesEntry() const;
  void handleNotesGet() const;
  void handleNotesSave() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleCover() const;
  void handleSleepImages() const;
  void handleRecentBooks() const;
  void handleGetBookProgress() const;
  void handleSleepCoverGet() const;
  void handleSleepCoverPin();
#if ENABLE_REMOTE_CONTROL
  void handleOpenBook();
  void handleRemoteButton();
#endif
  void handleScreenshot();
  void handleGetSettingsRaw() const;
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  bool isEpubFile(const String& filename) const;
  void handleUpload();
  void handleUploadPost();
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleOpdsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();
  void handleSetTime();

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    // Accumulates the first 8 file bytes across however many write chunks the
    // HTTP stack delivers, so the magic check works even if the first chunk is
    // < 8 bytes (TCP gives no minimum-segment guarantee).
    uint8_t magicHeader[8] = {0};
    size_t magicHeaderPos = 0;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::unique_ptr<uint8_t[]> buffer;
    size_t bufferPos = 0;
  } fontUpload;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();
  void handleTestOpdsServer();
  void handleKoreaderUseOpds();

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
  void handleForgetAllWifiNetworks();

  void handleMaintenanceValidateSleepImages();
  void handleMaintenanceClearCache();
  void handleMaintenanceResetSettings();
  void handleMaintenanceClearLogs();
  void handleMaintenanceClearCrashes();
};
