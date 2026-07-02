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


patch_simulator_hal(env)
