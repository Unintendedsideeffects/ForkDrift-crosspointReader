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
  * Simulator image-mock OOM: malloc-backed pixel buffers, header-only probes, and deferred PNG decoding.

Each edit is guarded so re-running (or a fresh libdep checkout that already
carries the fix) is a no-op.
"""

Import("env")  # noqa: F821
import os


def _sim_src_dir(env):
    libdeps = env.subst("$PROJECT_LIBDEPS_DIR") or os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    # The dep is named "simulator" and unpacks to <libdeps>/<env>/simulator/src.
    cand = os.path.join(libdeps, "simulator", "simulator", "src")
    return cand if os.path.isdir(cand) else None


def _replace_once(path, old, new, marker):
    """Apply old->new in `path` unless `marker` already present. Returns True if changed."""
    if not os.path.isfile(path):
        return False
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    if marker in content:
        return False  # already patched
    if old not in content:
        print("patch_simulator_hal: anchor not found in %s; skipping (upstream may have changed)" % path)
        return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(content.replace(old, new, 1))
    print("patch_simulator_hal: patched %s" % os.path.basename(path))
    return True


def patch_simulator_hal(env):
    src = _sim_src_dir(env)
    if not src:
        return

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
    network_moves = {
        'include "network/CrossPointWebServer.h"': 'include "network/server/CrossPointWebServer.h"',
        'include "network/FirmwareFlasher.h"': 'include "network/ota/FirmwareFlasher.h"',
        'include "network/OtaBootSwitch.h"': 'include "network/ota/OtaBootSwitch.h"',
        'include "network/OtaUpdater.h"': 'include "network/ota/OtaUpdater.h"',
    }
    for fname in ("CrossPointWebServer.cpp", "simulator_ota.cpp", "simulator_firmware.cpp"):
        for old, new in network_moves.items():
            _replace_once(os.path.join(src, fname), old, new, marker=new)

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

    # 5c) HalFile::getModifyDateTime: fork API the sim mock lacks; stub as
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

    # 5d) esp_http_client stub gaps: fork's HttpDownloader sets user_agent and
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

    # 5e) taskENTER/EXIT_CRITICAL(nullptr): valid on the single-core device
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


patch_simulator_hal(env)
