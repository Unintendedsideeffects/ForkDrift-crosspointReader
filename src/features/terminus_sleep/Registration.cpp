#include "features/terminus_sleep/Registration.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <InflateReader.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <Stream.h>
#include <WebServer.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "SpiBusMutex.h"
#include "core/features/FeatureCatalog.h"
#include "core/features/FeatureModules.h"
#include "core/registries/LifecycleRegistry.h"
#include "core/registries/WebRouteRegistry.h"
#include "features/terminus_sleep/RefreshEvidence.h"
#include "network/background/BackgroundWebServer.h"
#include "network/background/BackgroundWifiService.h"
#include "network/html/TerminusPluginPageHtml.generated.h"
#include "network/server/WebUtils.h"
#include "util/TerminusApi.h"
#include "util/TerminusCredentialStore.h"
#include "util/TerminusRefreshPolicy.h"
#include "util/TimeSync.h"
#include "util/WallClockInterval.h"

namespace features::terminus_sleep {

#if ENABLE_TERMINUS_SLEEP
namespace {

// Download lands on a temp name, then gets renamed to trmnl_latest.<ext>
// where <ext> matches the sniffed content type. The extension matters: the
// sleep renderer dispatches by it (SleepActivity isBmpFile vs
// renderImageSleepScreen), so a PNG pinned under .bmp renders nothing.
static constexpr const char* TRMNL_TEMP_PATH = "/sleep/trmnl_latest.dl";
static constexpr const char* TRMNL_DEST_BMP = "/sleep/trmnl_latest.bmp";
static constexpr const char* TRMNL_DEST_PNG = "/sleep/trmnl_latest.png";
static constexpr const char* TRMNL_DEST_JPG = "/sleep/trmnl_latest.jpg";
static constexpr size_t TRMNL_MAX_MANIFEST_BYTES = 16u * 1024u;
static constexpr int TRMNL_HTTP_BUFFER_BYTES = 2048;
static constexpr uint32_t TRMNL_FETCH_WAIT_CAP_MS = 120000;
// uzlib's streaming back-reference window, allocated as one block by
// InflateReader::init(true). Mirrors INFLATE_DICT_SIZE in that translation unit.
static constexpr size_t kInflateWindowBytes = 32768;

// Main-loop heartbeat state. Written by the dedicated fetch task and read by
// the main task; aligned 32-bit loads/stores are atomic on ESP32-C3.
static volatile uint32_t trmnlRefreshIntervalS = terminus_refresh::kDefaultIntervalS;
static volatile uint32_t trmnlLastAttemptEpoch = 0;
// Monotonic mirror of the above, recorded unconditionally. trmnlLastAttemptEpoch can only
// be written when the wall clock is usable, so without NTP it stays 0 and an epoch-based
// "due" test answers true on every background-server start, forever.
static volatile uint32_t trmnlLastAttemptMs = 0;
static volatile bool trmnlHaveAttempted = false;
// Where the last attempt got to, or why no attempt was made. Assigned static string literals only, so the volatile
// pointer swap is atomic and there is nothing to free. Reported by /api/terminus/status,
// which is the only way a client can learn the outcome: the fetch runs while the web
// server is deliberately stopped, so a client polling this endpoint during a fetch gets a
// connection failure, never fetch_running == true.
static const char* volatile fetchStage = "idle";

extern "C" esp_err_t arduino_esp_crt_bundle_attach(void* conf);

class BoundedManifestSink final : public Stream {
 public:
  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    const size_t room = (body_.size() < TRMNL_MAX_MANIFEST_BYTES) ? TRMNL_MAX_MANIFEST_BYTES - body_.size() : 0;
    const size_t take = size < room ? size : room;
    if (take > 0) {
      body_.append(reinterpret_cast<const char*>(buffer), take);
    }
    if (take < size) {
      overflowed_ = true;
    }
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  bool overflowed() const { return overflowed_; }
  std::string& body() { return body_; }

 private:
  std::string body_;
  bool overflowed_ = false;
};

struct VerifiedFileSink {
  const char* path = nullptr;
  HalFile file;
  size_t bytes = 0;
  bool opened = false;
  bool writeFailed = false;
  uint8_t magic[4] = {0, 0, 0, 0};
  size_t magicLen = 0;
};

// Map the first bytes of the downloaded image to the extension the sleep
// renderer keys its decoder choice on. Returns nullptr for unknown content.
static const char* destPathForMagic(const uint8_t* magic, size_t len) {
  if (len >= 2 && magic[0] == 'B' && magic[1] == 'M') {
    return TRMNL_DEST_BMP;
  }
  if (len >= 4 && magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') {
    return TRMNL_DEST_PNG;
  }
  if (len >= 2 && magic[0] == 0xFF && magic[1] == 0xD8) {
    return TRMNL_DEST_JPG;
  }
  return nullptr;
}

static esp_err_t manifestEventHandler(esp_http_client_event_t* evt) {
  auto* sink = static_cast<BoundedManifestSink*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && sink) {
    sink->write(static_cast<const uint8_t*>(evt->data), static_cast<size_t>(evt->data_len));
    if (sink->overflowed()) {
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static esp_err_t imageEventHandler(esp_http_client_event_t* evt) {
  auto* sink = static_cast<VerifiedFileSink*>(evt->user_data);
  if (evt->event_id != HTTP_EVENT_ON_DATA || !sink || !sink->opened) {
    return ESP_OK;
  }

  if (sink->magicLen < sizeof(sink->magic)) {
    const size_t take = static_cast<size_t>(evt->data_len) < sizeof(sink->magic) - sink->magicLen
                            ? static_cast<size_t>(evt->data_len)
                            : sizeof(sink->magic) - sink->magicLen;
    memcpy(sink->magic + sink->magicLen, evt->data, take);
    sink->magicLen += take;
  }

  SpiBusMutex::Guard guard;
  const size_t written =
      sink->file.write(reinterpret_cast<const uint8_t*>(evt->data), static_cast<size_t>(evt->data_len));
  sink->bytes += written;
  if (written != static_cast<size_t>(evt->data_len)) {
    sink->writeFailed = true;
    return ESP_FAIL;
  }
  return ESP_OK;
}

// Repack a downloaded PNG into a 1-bit BMP so the sleep screen never has to
// instantiate PNGdec.
//
// PNGdec is one 58,912-byte struct (a 32 KB inflate window, a 15,872-byte pixel
// buffer and an RGBA pipeline we do not use, all embedded to avoid malloc). The
// X4's largest free block is ~38,900 while awake and ~59,380 on the timed-wake
// path, so the pinned dashboard rendered only on the wake path and only by a
// 468-byte margin. Measured; see docs/FINDINGS.md 2026-08-15.
//
// PngToBmpConverter streams the same image through InflateReader -- the uzlib
// inflate the EPUB zip engine already uses -- whose 32 KB window is a separate,
// globally shared allocation. Peak contiguous demand drops from 58,912 to
// 32,768, which fits the awake heap with room to spare. Terminus already sends
// 1-bit greyscale (IHDR bit depth 1, colour type 0), so this is a repack rather
// than a conversion.
//
// Deliberately PNG-only: JPEG's decoder is ~20 KB and already fits, so routing
// it through here would add a transcode without buying any contiguity.
static bool repackPngAsBmp(const char* pngPath) {
  // uzlib needs a 32 KB back-reference window in ONE block. At fetch time WiFi
  // and the HTTP client are resident and the largest run measures ~26,612 on an
  // X4, so the allocation cannot succeed and the repack would fail after doing
  // real work and logging an error every refresh. Check first and stay quiet.
  //
  // This is a placement problem, not a design one: the same repack has ~38,900
  // to work with once the radio is down. Upstream solves it structurally by
  // lending the framebuffer as scratch (lib/Memory/BuildScratch + InflateStream,
  // neither of which ForkDrift has absorbed yet) -- once that lands, the window
  // comes from the loan and this precheck stops being the limiting factor.
  if (!heapguard::canAllocate(kInflateWindowBytes)) {
    LOG_DBG("TRMNL", "Repack skipped: no %u-byte block (free=%u largest=%u)",
            static_cast<unsigned>(kInflateWindowBytes), static_cast<unsigned>(heapguard::freeBytes()),
            static_cast<unsigned>(heapguard::largestBlock()));
    return false;
  }

  HalFile pngFile;
  if (!Storage.openFileForRead("TRMNL", pngPath, pngFile)) {
    LOG_ERR("TRMNL", "Repack: cannot reopen %s", pngPath);
    return false;
  }
  HalFile bmpFile;
  if (!Storage.openFileForWrite("TRMNL", TRMNL_DEST_BMP, bmpFile)) {
    LOG_ERR("TRMNL", "Repack: cannot open %s for write", TRMNL_DEST_BMP);
    return false;
  }
  LOG_INF("TRMNL", "Repack: PNG -> 1-bit BMP (free=%u largest=%u)", static_cast<unsigned>(heapguard::freeBytes()),
          static_cast<unsigned>(heapguard::largestBlock()));
  // Panel-native size, not the oriented viewport: the sleep renderer scales and
  // centres from whatever it is given, so keeping the source unrotated leaves
  // that decision where it already lives.
  const bool ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, bmpFile, HalDisplay::DISPLAY_WIDTH,
                                                                    HalDisplay::DISPLAY_HEIGHT, false);
  // Explicit close before the caller removes the temp file.
  pngFile.close();
  bmpFile.close();

  // Hand the 32 KB inflate window back. InflateReader keeps it allocated after
  // deinit() so repeated inflates can reuse it, which is right for reading a
  // book and wrong here: this path inflates once per refresh and the device
  // then sleeps. Leaving it held cost 32,868 bytes of free heap and dropped the
  // largest block from 40,948 to 9,204 on device -- worse, on the very metric
  // this repack exists to improve.
  InflateReader::releaseSharedWindow();

  if (!ok) {
    if (Storage.exists(TRMNL_DEST_BMP)) {
      Storage.remove(TRMNL_DEST_BMP);
    }
    return false;
  }
  return true;
}

// Download url to TRMNL_TEMP_PATH, sniff the content type, and move the file
// to the matching trmnl_latest.<ext>. Returns the final path, or nullptr on
// any failure (temp file is cleaned up).
static const char* downloadVerifiedImage(const std::string& url) {
  if (!terminus_api::isAllowedRemoteUrl(url)) {
    return nullptr;
  }

  const char* path = TRMNL_TEMP_PATH;
  VerifiedFileSink sink{};
  sink.path = path;
  {
    // SD and the e-ink panel share the SPI bus; every SD touch from this task
    // must hold SpiBusMutex or a concurrent display refresh trips the FreeRTOS
    // mutex-holder assert (queue.c:832) — that was the original on-device
    // crash, not the download itself.
    SpiBusMutex::Guard guard;
    Storage.ensureDirectoryExists("/sleep");
    if (Storage.exists(path)) {
      Storage.remove(path);
    }
    if (!Storage.openFileForWrite("TRMNL", path, sink.file)) {
      LOG_ERR("TRMNL", "Failed to open %s for image download", path);
      return nullptr;
    }
  }
  sink.opened = true;

  TimeSync::ensureTrustedClock();

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.event_handler = imageEventHandler;
  config.user_data = &sink;
  config.timeout_ms = 30000;
  config.buffer_size = TRMNL_HTTP_BUFFER_BYTES;
  config.buffer_size_tx = TRMNL_HTTP_BUFFER_BYTES;
  config.crt_bundle_attach = arduino_esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    sink.file.close();
    Storage.remove(path);
    LOG_ERR("TRMNL", "Failed to create image HTTP client");
    return nullptr;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  const esp_err_t err = esp_http_client_perform(client);
  const int code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  SpiBusMutex::Guard guard;
  sink.file.close();
  if (err != ESP_OK || code != 200 || sink.writeFailed || sink.bytes == 0) {
    Storage.remove(path);
    fetchStage = "image-failed";
    LOG_ERR("TRMNL", "Image download failed: HTTP %d err=%d written=%zu failed=%d", code, err, sink.bytes,
            sink.writeFailed ? 1 : 0);
    return nullptr;
  }

  const char* destPath = destPathForMagic(sink.magic, sink.magicLen);
  if (!destPath) {
    Storage.remove(path);
    fetchStage = "bad-image-type";
    LOG_ERR("TRMNL", "Image content not BMP/PNG/JPEG (magic %02x %02x %02x %02x)", sink.magic[0], sink.magic[1],
            sink.magic[2], sink.magic[3]);
    return nullptr;
  }

  // Drop every stale variant so an old pin under another extension can't
  // shadow or outlive the fresh image.
  for (const char* stale : {TRMNL_DEST_BMP, TRMNL_DEST_PNG, TRMNL_DEST_JPG}) {
    if (Storage.exists(stale)) {
      Storage.remove(stale);
    }
  }
  if (strcmp(destPath, TRMNL_DEST_PNG) == 0) {
    if (repackPngAsBmp(path)) {
      Storage.remove(path);
      LOG_INF("TRMNL", "Image downloaded (%zu bytes) -> %s (repacked from PNG)", sink.bytes, TRMNL_DEST_BMP);
      return TRMNL_DEST_BMP;
    }
    // Fall through and pin the PNG. The renderer may still manage it on the
    // timed-wake path, and a pinned PNG that sometimes renders beats no image.
    //
    // LOG_ERR, not LOG_WRN: a silent fallback here looks exactly like the
    // repack never running, which cost a full debugging cycle to tell apart on
    // a LOG_LEVEL=0 build where WRN is compiled out.
    LOG_ERR("TRMNL", "PNG->BMP repack failed; pinning the PNG instead (free=%u largest=%u)",
            static_cast<unsigned>(heapguard::freeBytes()), static_cast<unsigned>(heapguard::largestBlock()));
  }

  if (!Storage.rename(path, destPath)) {
    Storage.remove(path);
    LOG_ERR("TRMNL", "Failed to move image into place: %s", destPath);
    return nullptr;
  }

  LOG_INF("TRMNL", "Image downloaded (%zu bytes) -> %s", sink.bytes, destPath);
  return destPath;
}

// Poll /api/display, get image_url, download, pin as next sleep screen.
static bool fetchAndPinTrmnlImage() {
  // Snapshot credentials before creating the HTTP client. The fetch runs on a
  // separate FreeRTOS task, so retaining references to the store would make
  // header pointers unsafe if a web request changed the setup mid-transfer.
  const std::string apiKey = TERMINUS_STORE.apiKey();
  const std::string deviceId = TERMINUS_STORE.deviceId();
  const std::string deviceModel = TERMINUS_STORE.deviceModel();
  const std::string configuredBase = TERMINUS_STORE.baseUrl();
  if (apiKey.empty() || deviceId.empty()) {
    LOG_INF("TRMNL", "No Terminus credentials configured");
    return false;
  }

  const std::string base = terminus_api::normalizeBaseUrl(configuredBase);
  if (!terminus_api::isAllowedRemoteUrl(base)) {
    LOG_ERR("TRMNL", "Refusing non-HTTPS base URL: %s", base.c_str());
    return false;
  }
  const std::string displayUrl = base + "/api/display";

  LOG_INF("TRMNL", "Polling %s (model=%s)", displayUrl.c_str(), deviceModel.c_str());

  TimeSync::ensureTrustedClock();

  std::string manifest;
  {
    BoundedManifestSink sink;
    esp_http_client_config_t config = {};
    config.url = displayUrl.c_str();
    config.event_handler = manifestEventHandler;
    config.user_data = &sink;
    config.timeout_ms = 10000;
    config.buffer_size = TRMNL_HTTP_BUFFER_BYTES;
    config.buffer_size_tx = TRMNL_HTTP_BUFFER_BYTES;
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("TRMNL", "Failed to create HTTP client");
      return false;
    }

    esp_http_client_set_header(client, "ID", deviceId.c_str());
    esp_http_client_set_header(client, "Access-Token", apiKey.c_str());
    esp_http_client_set_header(client, "Device-Model", deviceModel.c_str());
    esp_http_client_set_header(client, "Firmware-Version", CROSSPOINT_VERSION);
    esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

    const esp_err_t err = esp_http_client_perform(client);
    const int code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && code == 200 && !sink.overflowed()) {
      manifest = std::move(sink.body());
      fetchStage = "manifest";
      LOG_INF("TRMNL", "Display manifest received (%zu bytes)", manifest.size());
    } else {
      fetchStage = "poll-failed";
      LOG_ERR("TRMNL", "Display poll failed: HTTP %d err=%d overflow=%d", code, err, sink.overflowed() ? 1 : 0);
    }
  }

  if (manifest.empty()) {
    return false;
  }

  terminus_api::DisplayManifest display;
  if (!terminus_api::parseDisplayManifest(manifest, display)) {
    fetchStage = "bad-manifest";
    LOG_ERR("TRMNL", "Invalid display manifest");
    return false;
  }

  trmnlRefreshIntervalS = display.refreshIntervalS;
  LOG_INF("TRMNL", "Server refresh interval: %u s", static_cast<unsigned>(trmnlRefreshIntervalS));

  LOG_INF("TRMNL", "Downloading image: %s", display.imageUrl.c_str());
  const char* pinnedPath = downloadVerifiedImage(display.imageUrl);
  if (!pinnedPath) {
    return false;
  }

  strncpy(SETTINGS.sleepPinnedPath, pinnedPath, sizeof(SETTINGS.sleepPinnedPath) - 1);
  SETTINGS.sleepPinnedPath[sizeof(SETTINGS.sleepPinnedPath) - 1] = '\0';
  bool settingsSaved = false;
  {
    SpiBusMutex::Guard guard;
    settingsSaved = SETTINGS.saveToFile();
  }
  if (!settingsSaved) {
    LOG_ERR("TRMNL", "Failed to persist Terminus sleep image settings");
    return false;
  }
  fetchStage = "ok";
  LOG_INF("TRMNL", "Terminus image pinned as next sleep screen");
  return true;
}

// fetchAndPinTrmnlImage nests SdFat writes inside esp_http_client_perform's
// data callback — too deep for the 8 KB bgwifi web-handler task (overflowed on
// the first successful image download). Run it on a dedicated task with the
// stack budget OtaWebCheckTask uses for the same workload; the 12 KB is
// heap-held only for the fetch's lifetime (a static stack would pin that DRAM
// permanently for a rare operation).
static constexpr uint32_t TRMNL_FETCH_TASK_STACK = 12288;
// FreeRTOS also allocates a TCB alongside the stack; leave room so the preflight below
// does not pass only for xTaskCreate to fail on the overhead.
static constexpr uint32_t TRMNL_FETCH_TASK_OVERHEAD = 1024;

static volatile bool fetchTaskRunning = false;
static volatile bool fetchTaskResult = false;
static volatile uint32_t fetchTaskRetryAfterMs = 0;
static volatile bool forcedFetchRequested = false;
static volatile uint32_t fetchDeferDeadlineMs = 0;

static bool fetchTaskRetryActive() {
  return fetchTaskRetryAfterMs != 0 && static_cast<int32_t>(millis() - fetchTaskRetryAfterMs) < 0;
}

static void recordFetchAttempt(const bool result) {
  fetchTaskResult = result;
  trmnlLastAttemptMs = millis();
  trmnlHaveAttempted = true;
  const auto nowEpoch = static_cast<uint32_t>(time(nullptr));
  if (wallclock::clockUsable(nowEpoch)) {
    trmnlLastAttemptEpoch = nowEpoch;
  }
  if (!result) {
    // Hard backoff, independent of the wall clock. Without this the only pacing after a
    // failure was refreshDue()'s failure interval, which needs a usable clock -- so an
    // unsynced device retried as fast as the server could be restarted.
    fetchTaskRetryAfterMs = millis() + terminus_refresh::kFailureRetryIntervalS * 1000UL;
  }
}

static void terminusFetchTask(void*) {
  const bool result = fetchAndPinTrmnlImage();
  recordFetchAttempt(result);
  fetchTaskRunning = false;
  vTaskDelete(nullptr);
}

static bool startFetchTask() {
  const auto decision =
      terminus_refresh::classifyStart(fetchTaskRunning, fetchTaskRetryActive(),
                                      heapguard::canAllocate(TRMNL_FETCH_TASK_STACK + TRMNL_FETCH_TASK_OVERHEAD));

  switch (decision) {
    case terminus_refresh::StartDecision::AlreadyRunning:
      LOG_INF("TRMNL", "Fetch already in progress");
      return false;
    case terminus_refresh::StartDecision::DeferBackoff:
      LOG_INF("TRMNL", "Fetch start deferred by retry backoff");
      return false;
    case terminus_refresh::StartDecision::DeferNoHeap:
      // Deliberately NOT recordFetchAttempt(false): a FreeRTOS task stack is a heap
      // allocation, and right after boot the device simply does not have ~12 KB contiguous
      // (measured: 6800 bytes free in the pre-server window). Waiting for the device to
      // settle is normal, not a failed fetch. Arm only the retry timer, which is also what
      // keeps onBackgroundServerTick from recycling the web server every second.
      fetchTaskRetryAfterMs = millis() + terminus_refresh::kFailureRetryIntervalS * 1000UL;
      fetchStage = "deferred-low-heap";
      LOG_INF("TRMNL", "Deferring fetch: no room for the %u B task stack (free=%zu, largest=%zu)",
              static_cast<unsigned>(TRMNL_FETCH_TASK_STACK + TRMNL_FETCH_TASK_OVERHEAD), heapguard::freeBytes(),
              heapguard::largestBlock());
      return false;
    case terminus_refresh::StartDecision::Start:
      break;
  }

  fetchTaskRunning = true;
  fetchTaskResult = false;
  fetchStage = "starting";
  if (xTaskCreate(&terminusFetchTask, "TerminusFetch", TRMNL_FETCH_TASK_STACK, nullptr, 1, nullptr) != pdPASS) {
    fetchTaskRunning = false;
    recordFetchAttempt(false);  // also arms the failure backoff
    LOG_ERR("TRMNL", "Failed to create fetch task");
    return false;
  }
  fetchTaskRetryAfterMs = 0;
  return true;
}

static void onStorageReady() { TERMINUS_STORE.load(); }

// Credentials as well as the setting: without this a configured-but-unpaired
// device spawned a 12 KB fetch task on every trigger only to fail inside
// fetchAndPinTrmnlImage().
static bool terminusFetchConfigured() {
  return CrossPointSettings::sleepModeActive(CrossPointSettings::TERMINUS_SLEEP) && TERMINUS_STORE.hasCredentials();
}

static bool hasPinnedTerminusImage() {
  return strncmp(SETTINGS.sleepPinnedPath, "/sleep/trmnl_latest.", strlen("/sleep/trmnl_latest.")) == 0 &&
         Storage.exists(SETTINGS.sleepPinnedPath);
}

static bool terminusFetchDue(const bool allowUnsetClock) {
  const auto nowEpoch = static_cast<uint32_t>(time(nullptr));
  if (!wallclock::clockUsable(nowEpoch)) {
    // Pace off millis() rather than answering "due" forever because no timestamp exists.
    return allowUnsetClock && terminus_refresh::refreshDueMonotonic(millis(), trmnlLastAttemptMs, trmnlHaveAttempted,
                                                                    fetchTaskResult, trmnlRefreshIntervalS);
  }
  return terminus_refresh::refreshDue(nowEpoch, trmnlLastAttemptEpoch, fetchTaskResult, trmnlRefreshIntervalS);
}

static bool waitForFetchTask(const uint32_t capMs) {
  const unsigned long deadline = millis() + capMs;
  while (fetchTaskRunning && millis() < deadline) {
    delay(50);
  }
  if (fetchTaskRunning) {
    LOG_ERR("TRMNL", "Fetch timed out after %u ms", capMs);
    return false;
  }
  return fetchTaskResult;
}

static void onBackgroundNetworkReady() {
  if (!TERMINUS_STORE.hasCredentials() || fetchTaskRunning || fetchTaskRetryActive() ||
      (!forcedFetchRequested && (!terminusFetchConfigured() || !terminusFetchDue(true)))) {
    return;
  }
  LOG_INF("TRMNL", "Network ready; fetching before background server startup");
  if (startFetchTask()) {
    forcedFetchRequested = false;
    fetchDeferDeadlineMs = millis() + TRMNL_FETCH_WAIT_CAP_MS;
  }
}

// Reported to whichever dispatcher started us. This function must never block: on the
// on-charge path onBackgroundNetworkReady is reached from BackgroundWebServer::startServer()
// via the main loop, so blocking here froze input and rendering for as long as the fetch
// took -- up to TRMNL_FETCH_WAIT_CAP_MS. The dispatcher decides how to wait instead.
static bool backgroundStartupDeferred() {
  if (!fetchTaskRunning) {
    return false;
  }
  if (static_cast<int32_t>(millis() - fetchDeferDeadlineMs) >= 0) {
    // Past the cap: let the server come up regardless. The fetch task is still alive and
    // will finish or fail on its own; holding the server down indefinitely is worse.
    LOG_ERR("TRMNL", "Fetch still running past %u ms cap; starting server anyway",
            static_cast<unsigned>(TRMNL_FETCH_WAIT_CAP_MS));
    return false;
  }
  return true;
}

static void onBackgroundServerTick() {
  if (!TERMINUS_STORE.hasCredentials() || fetchTaskRunning || fetchTaskRetryActive() ||
      (!forcedFetchRequested && (!terminusFetchConfigured() || !terminusFetchDue(false)))) {
    return;
  }

  // The running web server leaves too little contiguous heap for the 12 KB
  // fetch stack. Recycle only the active server while preserving STA; its next
  // pre-server hook performs the due fetch, then restores serving.
  if (BG_WIFI.isServing()) {
    LOG_INF("TRMNL", "Refresh due; recycling background WiFi server");
    BG_WIFI.stop(/*keepWifi=*/true);
  } else if (BackgroundWebServer::getInstance().isRunning()) {
    LOG_INF("TRMNL", "Refresh due; recycling on-charge web server");
    BackgroundWebServer::getInstance().stop(/*keepWifi=*/true);
  }
}

static bool shouldRegisterTerminusRoutes() { return core::FeatureCatalog::isEnabled("terminus_sleep"); }

static void appendMachineStatus(JsonDocument& doc) {
  doc["configured"] = TERMINUS_STORE.hasCredentials();
  doc["device_id"] = TERMINUS_STORE.deviceId().c_str();
  doc["device_model"] = TERMINUS_STORE.deviceModel().c_str();
  doc["base_url"] = TERMINUS_STORE.baseUrl().c_str();
  doc["has_api_key"] = !TERMINUS_STORE.apiKey().empty();
  doc["sleep_enabled"] = CrossPointSettings::sleepModeActive(CrossPointSettings::TERMINUS_SLEEP);
  doc["timed_refresh_interval"] = SETTINGS.timedSleepRefreshInterval;
  doc["fetch_pending"] = static_cast<bool>(forcedFetchRequested);
  doc["fetch_running"] = static_cast<bool>(fetchTaskRunning);
  doc["last_fetch_ok"] = static_cast<bool>(fetchTaskResult);
  doc["last_attempt_epoch"] = static_cast<uint32_t>(trmnlLastAttemptEpoch);
  // Outcome reporting, because progress reporting is impossible: the fetch runs while
  // the web server is deliberately stopped, so a client polling this endpoint during a
  // fetch gets a connection failure, never fetch_running == true.
  doc["last_stage"] = fetchStage ? fetchStage : "idle";
  doc["last_attempt_age_s"] = trmnlHaveAttempted ? static_cast<uint32_t>((millis() - trmnlLastAttemptMs) / 1000UL) : 0;
  doc["have_attempted"] = static_cast<bool>(trmnlHaveAttempted);
  doc["server_refresh_seconds"] = static_cast<uint32_t>(trmnlRefreshIntervalS);

  RefreshEvidence evidence;
  {
    SpiBusMutex::Guard guard;
    loadRefreshEvidence(evidence);
  }
  JsonDocument evidenceDoc;
  deserializeJson(evidenceDoc, refreshEvidenceJson(evidence));
  doc["timed_refresh_evidence"].set(evidenceDoc.as<JsonObjectConst>());
}

static void mountTerminusRoutes(WebServer* server) {
  server->on("/plugins/terminus", HTTP_GET, [server] {
    sendPrecompressedHtml(server, TerminusPluginPageHtml, TerminusPluginPageHtmlCompressedSize);
    LOG_DBG("WEB", "Served terminus plugin page");
  });

  server->on("/api/terminus/status", HTTP_GET, [server] {
    JsonDocument doc;
    appendMachineStatus(doc);
    std::string out;
    serializeJson(doc, out);
    server->send(200, "application/json", out.c_str());
  });

  server->on("/api/terminus/save", HTTP_POST, [server] {
    if (fetchTaskRunning) {
      server->send(409, "application/json", "{\"error\":\"fetch in progress; retry after it completes\"}");
      return;
    }
    if (!server->hasArg("plain")) {
      server->send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server->arg("plain"))) {
      server->send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    const char* apiKey = doc["api_key"] | "";
    const char* deviceId = doc["device_id"] | "";
    if (apiKey[0] == '\0' || deviceId[0] == '\0') {
      server->send(400, "application/json", "{\"error\":\"api_key and device_id required\"}");
      return;
    }
    const char* model = doc["device_model"] | "";
    const char* url = doc["base_url"] | "";
    const std::string baseUrl = terminus_api::normalizeBaseUrl(url);
    if (!terminus_api::isAllowedRemoteUrl(baseUrl)) {
      server->send(400, "application/json", "{\"error\":\"base_url must be https (or http to a private LAN IP)\"}");
      return;
    }

    const int timedRefreshInterval = doc["timed_refresh_interval"] | CrossPointSettings::TIMED_REFRESH_SCREENSAVER;
    if (timedRefreshInterval < CrossPointSettings::TIMED_REFRESH_OFF ||
        timedRefreshInterval >= CrossPointSettings::TIMED_SLEEP_REFRESH_MODE_COUNT) {
      server->send(400, "application/json", "{\"error\":\"invalid timed_refresh_interval\"}");
      return;
    }
    const bool sleepEnabled = doc["sleep_enabled"] | true;

    TERMINUS_STORE.setApiKey(apiKey);
    TERMINUS_STORE.setDeviceId(deviceId);
    TERMINUS_STORE.setDeviceModel(model[0] == '\0' ? "xteink_x4" : model);
    TERMINUS_STORE.setBaseUrl(baseUrl);
    SETTINGS.terminusSleepEnabled = sleepEnabled ? 1 : 0;  // Legacy serialized mirror.
    if (sleepEnabled) {
      SETTINGS.sleepScreenSplit = CrossPointSettings::SLEEP_SPLIT_UNIFIED;
      SETTINGS.sleepScreen = CrossPointSettings::TERMINUS_SLEEP;
    } else {
      if (SETTINGS.sleepScreen == CrossPointSettings::TERMINUS_SLEEP) SETTINGS.sleepScreen = CrossPointSettings::DARK;
      if (SETTINGS.sleepScreenReader == CrossPointSettings::TERMINUS_SLEEP)
        SETTINGS.sleepScreenReader = CrossPointSettings::DARK;
      if (SETTINGS.sleepScreenHome == CrossPointSettings::TERMINUS_SLEEP)
        SETTINGS.sleepScreenHome = CrossPointSettings::DARK;
    }
    SETTINGS.timedSleepRefreshInterval = static_cast<uint8_t>(timedRefreshInterval);

    bool saved = false;
    {
      SpiBusMutex::Guard guard;
      saved = TERMINUS_STORE.save() && SETTINGS.saveToFile();
    }
    if (!saved) {
      server->send(500, "application/json", "{\"error\":\"failed to persist Terminus setup\"}");
      return;
    }
    forcedFetchRequested = sleepEnabled;
    server->send(200, "application/json",
                 sleepEnabled ? "{\"status\":\"ok\",\"message\":\"Terminus setup saved; fetch scheduled\"}"
                              : "{\"status\":\"ok\",\"message\":\"Terminus setup saved\"}");
  });

  server->on("/api/terminus/test", HTTP_POST, [server] {
    if (!TERMINUS_STORE.hasCredentials()) {
      server->send(400, "application/json", "{\"error\":\"not configured\"}");
      return;
    }
    if (fetchTaskRunning) {
      server->send(409, "application/json", "{\"error\":\"fetch already in progress\"}");
      return;
    }
    // A route-heavy running server leaves too little contiguous heap for the
    // proven-safe 12 KB fetch task. Acknowledge first, then let the main-loop
    // heartbeat recycle this server and fetch in the pre-server window.
    forcedFetchRequested = true;
    server->send(202, "application/json",
                 "{\"status\":\"accepted\",\"message\":\"Fetch scheduled; the web server will restart briefly\"}");
  });

  server->on("/api/terminus/clear", HTTP_POST, [server] {
    if (fetchTaskRunning) {
      server->send(409, "application/json", "{\"error\":\"fetch in progress; retry after it completes\"}");
      return;
    }
    bool saved = false;
    {
      SpiBusMutex::Guard guard;
      TERMINUS_STORE.clear();
      SETTINGS.terminusSleepEnabled = 0;
      if (SETTINGS.sleepScreen == CrossPointSettings::TERMINUS_SLEEP) SETTINGS.sleepScreen = CrossPointSettings::DARK;
      if (SETTINGS.sleepScreenReader == CrossPointSettings::TERMINUS_SLEEP)
        SETTINGS.sleepScreenReader = CrossPointSettings::DARK;
      if (SETTINGS.sleepScreenHome == CrossPointSettings::TERMINUS_SLEEP)
        SETTINGS.sleepScreenHome = CrossPointSettings::DARK;
      forcedFetchRequested = false;
      saved = SETTINGS.saveToFile();
    }
    if (!saved) {
      server->send(500, "application/json", "{\"error\":\"failed to persist cleared setup\"}");
      return;
    }
    server->send(200, "application/json", "{\"status\":\"ok\"}");
  });
}

}  // namespace
#endif

void registerFeature() {
#if ENABLE_TERMINUS_SLEEP
  if (!core::FeatureModules::hasCapability(core::Capability::TerminusSleep)) {
    return;
  }

  core::LifecycleEntry entry{};
  entry.onStorageReady = onStorageReady;
  entry.onBackgroundNetworkReady = onBackgroundNetworkReady;
  entry.backgroundStartupDeferred = backgroundStartupDeferred;
  entry.onBackgroundServerTick = onBackgroundServerTick;
  core::LifecycleRegistry::add(entry);

  core::WebRouteEntry webRouteEntry{};
  webRouteEntry.routeId = "terminus_plugin";
  webRouteEntry.shouldRegister = shouldRegisterTerminusRoutes;
  webRouteEntry.mountRoutes = mountTerminusRoutes;
  core::WebRouteRegistry::add(webRouteEntry);
#endif
}

#if ENABLE_TERMINUS_SLEEP
bool startTrmnlFetchAndWait(uint32_t capMs) {
  if (!startFetchTask()) {
    return false;
  }
  return waitForFetchTask(capMs);
}

bool shouldRefreshBeforeSleep() {
  return terminusFetchConfigured() && (!hasPinnedTerminusImage() || terminusFetchDue(true));
}

uint32_t currentRefreshIntervalSeconds() { return trmnlRefreshIntervalS; }

std::string machineStatusJson() {
  JsonDocument doc;
  appendMachineStatus(doc);
  std::string json;
  serializeJson(doc, json);
  return json;
}
#else
bool startTrmnlFetchAndWait(uint32_t) { return false; }
bool shouldRefreshBeforeSleep() { return false; }
uint32_t currentRefreshIntervalSeconds() { return terminus_refresh::kDefaultIntervalS; }
std::string machineStatusJson() { return R"({"configured":false,"timed_refresh_evidence":{"schema_version":1}})"; }
#endif

}  // namespace features::terminus_sleep
