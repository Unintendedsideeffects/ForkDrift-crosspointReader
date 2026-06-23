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


patch_simulator_hal(env)
