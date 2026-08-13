"""
PlatformIO pre-build script (simulator env): sync the external crosspoint-simulator
HAL with the firmware HAL when it has drifted behind.

The desktop simulator's HAL lives in an external libdep
(.pio/libdeps/simulator/simulator/src, from uxjulia/crosspoint-simulator). When
the firmware HAL (lib/hal) gains a member that the simulator copy lacks, the
simulator build breaks while the device build stays green -- classic harness
drift. This applies the missing pieces idempotently so `pio run -e simulator`
matches what src/main.cpp now expects:

  * HalGPIO::WakeupReason gains the TimerRefresh enumerator (timed sleep refresh).
  * HalPowerManager::startDeepSleep gains the 2-arg overload
    (HalGPIO&, uint64_t timerWakeupMicros = 0); the simulator ignores the timer.
  * OtaUpdater gains non-destructive feature-store catalog stubs.
  * Simulator image-mock OOM: malloc-backed pixel buffers, header-only probes, and deferred PNG decoding.

Each edit is guarded so re-running (or a fresh libdep checkout that already
carries the fix) is a no-op.
"""

import os

try:
    Import("env")  # noqa: F821
except NameError:
    # Standalone execution fallback
    class DummyEnv:
        def __getitem__(self, key):
            if key == "PROJECT_DIR":
                return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
            raise KeyError(key)
        def subst(self, s):
            if s == "$PROJECT_LIBDEPS_DIR":
                return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".pio", "libdeps"))
            if s == "$PIOENV":
                return "simulator"
            return s
    env = DummyEnv()

_missing_patches = []


def _sim_src_dir(env):
    libdeps = env.subst("$PROJECT_LIBDEPS_DIR") or os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    # The dep is named "simulator" and unpacks to <libdeps>/<env>/simulator/src.
    pioenv = env.subst("$PIOENV")
    candidates = (
        os.path.join(libdeps, pioenv, "simulator", "src"),
        os.path.join(libdeps, "simulator", "src"),
    )
    return next((candidate for candidate in candidates if os.path.isdir(candidate)), None)


def _replace_once(path, old, new, marker):
    """Apply old->new in `path` unless `marker` already present. Returns status."""
    if not os.path.isfile(path):
        _missing_patches.append((path, marker))
        return "missing"
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    if marker in content:
        return "already"  # already patched
    if old not in content:
        _missing_patches.append((path, marker))
        return "missing"
    with open(path, "w", encoding="utf-8") as f:
        f.write(content.replace(old, new, 1))
    print("patch_simulator_hal: patched %s" % os.path.basename(path))
    return "patched"


def patch_simulator_hal(env):
    src = _sim_src_dir(env)
    if not src:
        return

    _missing_patches.clear()

    # 1) WakeupReason::TimerRefresh
    _replace_once(
        os.path.join(src, "HalGPIO.h"),
        "enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };",
        "enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, TimerRefresh, Other };",
        marker="TimerRefresh",
    )

    # 2) startDeepSleep 2-arg overload (declaration)
    _replace_once(
        os.path.join(src, "HalPowerManager.h"),
        "void startDeepSleep(HalGPIO &gpio) const;",
        "void startDeepSleep(HalGPIO &gpio, uint64_t timerWakeupMicros = 0) const;",
        marker="timerWakeupMicros",
    )

    # 3) startDeepSleep 2-arg overload (definition) -- simulator ignores the timer.
    _replace_once(
        os.path.join(src, "HalPowerManager.cpp"),
        "void HalPowerManager::startDeepSleep(HalGPIO &gpio) const { gpio.startDeepSleep(); }",
        "void HalPowerManager::startDeepSleep(HalGPIO &gpio, uint64_t /*timerWakeupMicros*/) const { "
        "gpio.startDeepSleep(); }",
        marker="timerWakeupMicros",
    )

    # 4) network/ header moves: the firmware's refactor(network) split src/network
    #    into subdirs (server/, ota/). The sim shims still include the old flat
    #    paths; remap each to its new home. (marker == the new path -> idempotent)
    _replace_once(
        os.path.join(src, "CrossPointWebServer.cpp"),
        'include "network/CrossPointWebServer.h"',
        'include "network/server/CrossPointWebServer.h"',
        marker='include "network/server/CrossPointWebServer.h"',
    )
    _replace_once(
        os.path.join(src, "simulator_ota.cpp"),
        'include "network/OtaUpdater.h"',
        'include "network/ota/OtaUpdater.h"',
        marker='include "network/ota/OtaUpdater.h"',
    )
    _replace_once(
        os.path.join(src, "simulator_firmware.cpp"),
        'include "network/FirmwareFlasher.h"',
        'include "network/ota/FirmwareFlasher.h"',
        marker='include "network/ota/FirmwareFlasher.h"',
    )
    _replace_once(
        os.path.join(src, "simulator_firmware.cpp"),
        'include "network/OtaBootSwitch.h"',
        'include "network/ota/OtaBootSwitch.h"',
        marker='include "network/ota/OtaBootSwitch.h"',
    )

    # 5) String mock gaps: firmware uses Arduino String members the mock lacks.
    _replace_once(
        os.path.join(src, "WString.h"),
        "class String {\npublic:\n  std::string s;",
        "class String {\npublic:\n  std::string s;\n"
        "  void reserve(size_t n) { s.reserve(n); }",
        marker="void reserve(size_t n)",
    )

    # 5b) OtaUpdater::installUpdate: fork signature is no-arg (progress polled
    #     from members); rewrite the sim stub's non-CROSSINK variant to match.
    _replace_once(
        os.path.join(src, "simulator_ota.cpp"),
        "OtaUpdater::OtaUpdaterError\n"
        "OtaUpdater::installUpdate(ProgressCallback onProgress, void *ctx) {\n"
        '  LOG_DBG("OTA", "[SIM] OTA install is not supported in the native simulator");\n'
        "  processedSize = 1;\n"
        "  totalSize = 1;\n"
        "  if (onProgress)\n"
        "    onProgress(ctx);\n"
        "  return INTERNAL_UPDATE_ERROR;\n"
        "}",
        "OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {\n"
        '  LOG_DBG("OTA", "[SIM] OTA install is not supported in the native simulator");\n'
        "  processedSize = 1;\n"
        "  totalSize = 1;\n"
        "  return INTERNAL_UPDATE_ERROR;\n"
        "}",
        marker="OtaUpdater::installUpdate() {",
    )

    # 5c) Feature-store OTA methods added after the pinned simulator revision.
    #     Keep native runs offline and expose a useful unsupported diagnostic.
    _replace_once(
        os.path.join(src, "simulator_ota.cpp"),
        "OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {\n"
        '  LOG_DBG("OTA", "[SIM] OTA check is non-destructive; reporting no update");\n'
        "  return NO_UPDATE;\n"
        "}",
        "OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {\n"
        '  LOG_DBG("OTA", "[SIM] OTA check is non-destructive; reporting no update");\n'
        "  return NO_UPDATE;\n"
        "}\n\n"
        "bool OtaUpdater::loadFeatureStoreCatalog() {\n"
        "  featureStoreEntries.clear();\n"
        "  lastError = CATALOG_UNAVAILABLE_ERROR;\n"
        "  return false;\n"
        "}\n"
        "bool OtaUpdater::hasFeatureStoreCatalog() const { return !featureStoreEntries.empty(); }\n"
        "const std::vector<OtaUpdater::FeatureStoreEntry>& OtaUpdater::getFeatureStoreEntries() const {\n"
        "  return featureStoreEntries;\n"
        "}\n"
        "bool OtaUpdater::selectFeatureStoreBundleByIndex(size_t) {\n"
        "  lastError = BUNDLE_UNAVAILABLE_ERROR;\n"
        "  return false;\n"
        "}\n"
        "const String& OtaUpdater::getLastError() const { return lastError; }",
        marker="OtaUpdater::loadFeatureStoreCatalog()",
    )

    # 5d) HalFile::getModifyDateTime: fork API the sim mock lacks; stub as
    #     "no timestamp available" (callers fall back to size-only checks).
    _replace_once(
        os.path.join(src, "HalStorage.h"),
        "bool seek(size_t pos);",
        "bool seek(size_t pos);\n"
        "  bool getModifyDateTime(uint16_t *pdate, uint16_t *ptime) {\n"
        "    if (pdate) *pdate = 0;\n"
        "    if (ptime) *ptime = 0;\n"
        "    return false;\n"
        "  }",
        marker="getModifyDateTime",
    )

    # 5e) esp_http_client stub gaps: fork's HttpDownloader sets user_agent and
    #     handles EAGAIN; add the field and the errno constant.
    _replace_once(
        os.path.join(src, "esp_http_client.h"),
        "  const char *url = nullptr;",
        "  const char *url = nullptr;\n  const char *user_agent = nullptr;",
        marker="user_agent",
    )
    _replace_once(
        os.path.join(src, "esp_http_client.h"),
        "struct esp_http_client_config_t {",
        "#ifndef ESP_ERR_HTTP_EAGAIN\n"
        "#define ESP_ERR_HTTP_EAGAIN 0x7290\n"
        "#endif\n"
        "struct esp_http_client_config_t {",
        marker="ESP_ERR_HTTP_EAGAIN",
    )

    # 5e2) esp_mac.h: the fork reads the station MAC directly, because
    #      WiFi.macAddress() needs the STA netif to exist and WifiSelectionActivity
    #      is routinely entered with the radio off. The sim stub only offers
    #      esp_efuse_mac_get_default(), so add the two symbols the fork uses and
    #      keep the same fake MAC so screenshots stay stable.
    _replace_once(
        os.path.join(src, "esp_mac.h"),
        "// Simulator stub: return a fixed fake MAC address",
        "typedef enum { ESP_MAC_WIFI_STA = 0, ESP_MAC_WIFI_SOFTAP = 1, ESP_MAC_BT = 2, ESP_MAC_ETH = 3 } esp_mac_type_t;\n"
        "\n"
        "// Simulator stub: return a fixed fake MAC address",
        marker="ESP_MAC_WIFI_STA",
    )
    _replace_once(
        os.path.join(src, "esp_mac.h"),
        "  memcpy(mac, fakeMac, 6);\n  return 0;\n}",
        "  memcpy(mac, fakeMac, 6);\n  return 0;\n}\n"
        "\n"
        "static inline int esp_read_mac(uint8_t *mac, esp_mac_type_t) { return esp_efuse_mac_get_default(mac); }",
        marker="esp_read_mac",
    )

    # 5e3) Async refresh seam (fork commit f518ba446). The device splits
    #      displayBuffer() into "start the panel refresh" and "wait, then re-sync
    #      the RED baseline", so the reader can build the next chapter during the
    #      ~1.5 s a half refresh takes. The simulator paints instantly and has no
    #      BUSY line, so the honest mock is: async == synchronous, finish == no-op.
    #      That keeps every caller's pairing exercised without pretending to model
    #      a window that does not exist here.
    _replace_once(
        os.path.join(src, "HalDisplay.h"),
        "  void displayWindow(int x, int y, int w, int h);",
        "  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false) {\n"
        "    displayBuffer(mode, turnOffScreen);\n"
        "  }\n"
        "  void finishDisplayBuffer() {}\n"
        "  bool supportsAsyncRefresh() const { return false; }\n"
        "  void displayWindow(int x, int y, int w, int h);",
        marker="displayBufferAsync",
    )

    # 5e4) esp_get_free_heap_size(): the stub returns a flat 1,000,000, which is
    #      not a mis-tuned number -- it silently disables every gate built on it.
    #      The reader guards page render (EpubReaderActivity.cpp:1742) and font
    #      prewarm (:2073) on this call, so under the stub both gates pass
    #      unconditionally and the simulator can never reproduce the low-heap
    #      paths they exist to protect. heapguard already routes through
    #      ESP.getFreeHeap() and so was always budget-backed; this makes the
    #      direct callers agree with it instead of seeing a heap 5.5x larger.
    #      Declaration only -- sim_heap.cpp owns the definition, so there is one
    #      budget and one accounting mutex rather than two disagreeing sources.
    _replace_once(
        os.path.join(src, "esp_system.h"),
        "inline uint32_t esp_get_free_heap_size() { return 1000000; }",
        "uint32_t esp_get_free_heap_size();  // defined in src/simulator/sim_heap.cpp",
        marker="defined in src/simulator/sim_heap.cpp",
    )

    # 5f) taskENTER/EXIT_CRITICAL(nullptr): valid on the single-core device
    #     port (global interrupt disable); the sim mock derefs the mux. Route
    #     nullptr to a shared global mutex.
    _replace_once(
        os.path.join(src, "freertos", "FreeRTOS.h"),
        "inline void taskENTER_CRITICAL(portMUX_TYPE *mux) { mux->mtx.lock(); }",
        "inline SimPortMux &simGlobalPortMux() {\n"
        "  static SimPortMux mux;\n"
        "  return mux;\n"
        "}\n"
        "inline void taskENTER_CRITICAL(portMUX_TYPE *mux) { (mux ? *mux : simGlobalPortMux()).mtx.lock(); }",
        marker="simGlobalPortMux",
    )
    _replace_once(
        os.path.join(src, "freertos", "FreeRTOS.h"),
        "inline void taskEXIT_CRITICAL(portMUX_TYPE *mux) { mux->mtx.unlock(); }",
        "inline void taskEXIT_CRITICAL(portMUX_TYPE *mux) { (mux ? *mux : simGlobalPortMux()).mtx.unlock(); }",
        marker="simGlobalPortMux()).mtx.unlock",
    )

    # 6) HalDisplay mock gaps: tiled grayscale strip API (fork SDK feature).
    #    The sim composites nothing; report unsupported so the firmware takes
    #    the storeBwBuffer fallback path, which the sim renders correctly.
    _replace_once(
        os.path.join(src, "HalDisplay.h"),
        "void displayGrayBuffer(bool turnOffScreen = false);",
        "void displayGrayBuffer(bool turnOffScreen = false);\n"
        "  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t *rows, uint16_t yStart, uint16_t numRows) {}\n"
        "  bool supportsStripGrayscale() const { return false; }",
        marker="supportsStripGrayscale",
    )

    # 7) Rule 1: malloc-backed pixel buffer + header-only probe
    _replace_once(
        os.path.join(src, "SimulatorImageDecode.h"),
        "#include <vector>",
        "#include <cstdlib>\n#include <utility>",
        marker="#include <cstdlib>",
    )
    _replace_once(
        os.path.join(src, "SimulatorImageDecode.h"),
        '''struct DecodedImage {
  int width{0};
  int height{0};
  int channels{0};
  bool hasAlpha{false};
  std::vector<uint8_t> pixels;
};

bool decodeImageBytes(const uint8_t *data, size_t size, int requestedChannels,
                      DecodedImage &out);''',
        '''// Malloc-backed byte buffer: the simulator heap budget only tracks operator
// new, and these buffers emulate on-device STREAMING decode paths that never
// materialize a full frame — they must not count against the device budget.
class PixelBuffer {
 public:
  PixelBuffer() = default;
  PixelBuffer(PixelBuffer &&other) noexcept { swap(other); }
  PixelBuffer &operator=(PixelBuffer &&other) noexcept {
    if (this != &other) { reset(); swap(other); }
    return *this;
  }
  PixelBuffer(const PixelBuffer &) = delete;
  PixelBuffer &operator=(const PixelBuffer &) = delete;
  ~PixelBuffer() { reset(); }

  bool allocate(size_t n) {
    reset();
    if (n == 0) return false;
    ptr_ = static_cast<uint8_t *>(std::malloc(n));
    if (!ptr_) return false;
    size_ = n;
    return true;
  }
  void reset() {
    std::free(ptr_);
    ptr_ = nullptr;
    size_ = 0;
  }
  void swap(PixelBuffer &other) noexcept {
    std::swap(ptr_, other.ptr_);
    std::swap(size_, other.size_);
  }
  uint8_t *data() { return ptr_; }
  const uint8_t *data() const { return ptr_; }
  size_t size() const { return size_; }
  bool empty() const { return ptr_ == nullptr || size_ == 0; }

 private:
  uint8_t *ptr_{nullptr};
  size_t size_{0};
};

struct DecodedImage {
  int width{0};
  int height{0};
  int channels{0};
  bool hasAlpha{false};
  PixelBuffer pixels;
};

bool decodeImageBytes(const uint8_t *data, size_t size, int requestedChannels,
                      DecodedImage &out);
// Header-only probe (stbi_info): dimensions + alpha without decoding pixels.
bool probeImageInfo(const uint8_t *data, size_t size, int &width, int &height,
                    bool &hasAlpha);''',
        marker="PixelBuffer",
    )

    # 8) Rule 2: hand pixels to PixelBuffer + implement probe
    _replace_once(
        os.path.join(src, "SimulatorImageDecode.cpp"),
        '#include "SimulatorImageDecode.h"',
        '#include "SimulatorImageDecode.h"\n#include <cstring>',
        marker='#include <cstring>',
    )
    _replace_once(
        os.path.join(src, "SimulatorImageDecode.cpp"),
        '''  const size_t byteCount =
      static_cast<size_t>(width) * static_cast<size_t>(height) *
      static_cast<size_t>(requestedChannels);
  out.width = width;
  out.height = height;
  out.channels = requestedChannels;
  out.hasAlpha = sourceChannels == 2 || sourceChannels == 4;
  out.pixels.assign(decoded, decoded + byteCount);
  stbi_image_free(decoded);
  return true;
}''',
        '''  const size_t byteCount =
      static_cast<size_t>(width) * static_cast<size_t>(height) *
      static_cast<size_t>(requestedChannels);
  out.width = width;
  out.height = height;
  out.channels = requestedChannels;
  out.hasAlpha = sourceChannels == 2 || sourceChannels == 4;
  if (!out.pixels.allocate(byteCount)) {
    stbi_image_free(decoded);
    out = DecodedImage{};
    return false;
  }
  memcpy(out.pixels.data(), decoded, byteCount);
  stbi_image_free(decoded);
  return true;
}

bool probeImageInfo(const uint8_t *data, size_t size, int &width, int &height,
                    bool &hasAlpha) {
  width = 0;
  height = 0;
  hasAlpha = false;
  int channels = 0;
  if (!data || size == 0 ||
      !stbi_info_from_memory(data, static_cast<int>(size), &width, &height,
                             &channels)) {
    return false;
  }
  hasAlpha = channels == 2 || channels == 4;
  return width > 0 && height > 0;
}''',
        marker="probeImageInfo",
    )

    # 9) Rule 3: probe on open, decode lazily
    _replace_once(
        os.path.join(src, "PNGdec.h"),
        '''    std::vector<uint8_t> encoded(static_cast<size_t>(size));
    PNGFILE file{handle};
    int32_t totalRead = 0;
    while (totalRead < size) {
      const int32_t bytesRead =
          readCb(&file, encoded.data() + totalRead, size - totalRead);
      if (bytesRead <= 0) {
        break;
      }
      totalRead += bytesRead;
    }
    closeCb(handle);

    if (totalRead <= 0 ||
        !simulator_image::decodeImageBytes(encoded.data(),
                                           static_cast<size_t>(totalRead), 4,
                                           image_)) {
      return PNG_INVALID_FILE;
    }

    return PNG_SUCCESS;
  }''',
        '''    if (!encoded_.allocate(static_cast<size_t>(size))) {
      closeCb(handle);
      return PNG_INVALID_FILE;
    }
    PNGFILE file{handle};
    int32_t totalRead = 0;
    while (totalRead < size) {
      const int32_t bytesRead =
          readCb(&file, encoded_.data() + totalRead, size - totalRead);
      if (bytesRead <= 0) {
        break;
      }
      totalRead += bytesRead;
    }
    closeCb(handle);

    // Probe header only; the full decode is deferred to decode() so dimension
    // queries (getDimensionsStatic) never pay a whole-image decode.
    bool hasAlpha = false;
    if (totalRead <= 0 ||
        !simulator_image::probeImageInfo(encoded_.data(),
                                         static_cast<size_t>(totalRead),
                                         image_.width, image_.height,
                                         hasAlpha)) {
      encoded_.reset();
      return PNG_INVALID_FILE;
    }
    image_.hasAlpha = hasAlpha;
    encodedBytes_ = static_cast<size_t>(totalRead);

    return PNG_SUCCESS;
  }''',
        marker="deferred to decode()",
    )
    _replace_once(
        os.path.join(src, "PNGdec.h"),
        '''  int decode(void *user, int) {
    if (!drawCb_ || image_.pixels.empty() || image_.width <= 0 ||
        image_.height <= 0) {
      return PNG_INVALID_FILE;
    }''',
        '''  int decode(void *user, int) {
    if (image_.pixels.empty() && !encoded_.empty()) {
      // Lazy decode: open() only probed the header.
      if (!simulator_image::decodeImageBytes(encoded_.data(), encodedBytes_, 4,
                                             image_)) {
        return PNG_INVALID_FILE;
      }
      encoded_.reset();
    }
    if (!drawCb_ || image_.pixels.empty() || image_.width <= 0 ||
        image_.height <= 0) {
      return PNG_INVALID_FILE;
    }''',
        marker="Lazy decode: open() only probed the header",
    )
    _replace_once(
        os.path.join(src, "PNGdec.h"),
        '  void close() { image_ = simulator_image::DecodedImage{}; }',
        '  void close() {\n'
        '    image_ = simulator_image::DecodedImage{};\n'
        '    encoded_.reset();\n'
        '    encodedBytes_ = 0;\n'
        '  }',
        marker="encodedBytes_ = 0;",
    )
    _replace_once(
        os.path.join(src, "PNGdec.h"),
        '''private:
  simulator_image::DecodedImage image_;
  PNG_DRAW_CALLBACK drawCb_{nullptr};''',
        '''private:
  simulator_image::DecodedImage image_;
  PNG_DRAW_CALLBACK drawCb_{nullptr};
  simulator_image::PixelBuffer encoded_;
  size_t encodedBytes_{0};''',
        marker="encodedBytes_{0};",
    )

    # 10) esp_ota_set_boot_partition stub in esp_ota_ops.h
    _replace_once(
        os.path.join(src, "esp_ota_ops.h"),
        "inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() {\n  return ESP_OK;\n}",
        "inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() {\n  return ESP_OK;\n}\n\n"
        "inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t *) { return ESP_OK; }",
        marker="esp_ota_set_boot_partition",
    )

    if _missing_patches:
        for path, marker in _missing_patches:
            print("patch_simulator_hal: missing anchor for marker '%s' in file %s" % (marker, path))
        raise SystemExit(
            "patch_simulator_hal: %d required anchors missing — upstream drifted; re-sync anchors and the pin in platformio.ini"
            % len(_missing_patches)
        )


patch_simulator_hal(env)
