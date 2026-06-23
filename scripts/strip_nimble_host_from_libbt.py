"""
PlatformIO pre-build script: strip the duplicate IDF NimBLE host out of the
prebuilt esp32c3 ``libbt.a`` so only h2zero/NimBLE-Arduino's bundled host links.

Why this exists
---------------
arduino-esp32 3.x ships ``libbt.a`` built with ``CONFIG_BT_NIMBLE_ENABLED=y`` —
a *complete* IDF NimBLE host + porting layer (NPL). The ``h2zero/NimBLE-Arduino``
library *also* bundles its own full host under ``src/nimble/``. Both archives
export the same global symbols (``nimble_port_init``, ``ble_gap_*``,
``npl_freertos_*`` …). They both link (static-archive, first-wins resolution);
at runtime the controller uses one copy's NPL static state while the host uses
the other's, so a mutex created in one storage is pended through the other ->
``npl_freertos_mutex_pend`` asserts on a NULL handle -> panic at BLE start.

The fix
-------
On the ESP32-C3 the BLE controller talks to the host *only* over VHCI
(``API_vhci_host_send_packet`` & friends) — it never references the NimBLE host
or the NPL. So the IDF host objects can be removed from ``libbt.a`` with no
effect on the controller. We delete exactly the ``libbt.a`` objects whose
basename also appears in NimBLE-Arduino's source tree (the genuine duplicates),
leaving the controller bridge (``bt.c.obj``), BLE-Mesh, BLUFI and OSI objects
intact. NimBLE-Arduino's bundled host then becomes the sole host -> one NPL ->
no split -> no panic.

Computing the strip list as a basename *intersection* against the project's
actual NimBLE-Arduino checkout makes this self-adjusting: bump the framework or
the library and the set recomputes; there is no hand-maintained file list to rot.

Safety / idempotency
---------------------
``libbt.a.orig`` holds the pristine archive. If the live ``libbt.a`` still
contains a known host object it is treated as pristine: we (re)snapshot it to
``.orig`` and re-strip. A fresh archive dropped in by a framework update always
contains the host objects, so an update is detected automatically and re-stripped
against the latest pristine copy. Once stripped, re-runs are no-ops.

NOTE: this mutates the *shared* framework package
(``framework-arduinoespressif32-libs/esp32c3/lib/libbt.a``). That is acceptable
here because the result (NimBLE host removed, controller intact) is exactly what
any NimBLE-Arduino-based esp32c3 project on this machine wants, and it is fully
reversible by restoring ``libbt.a.orig``. A Bluedroid-only project is unaffected
(it never used the NimBLE host); a project using the *IDF* NimBLE host directly
would be — restore the backup for that case.
"""

Import("env")  # noqa: F821
import os
import shutil
import subprocess

# Presence of this object means the archive is pristine (host not yet stripped).
_HOST_MARKER_OBJ = "ble_hs.c.obj"


def _ar_ranlib(env):
    """Resolve the archiver / index tools, preferring PlatformIO's own vars."""
    ar = env.subst("$AR") or "ar"
    ranlib = env.subst("$RANLIB")
    if not ranlib:
        # Derive ranlib next to ar (…-ar -> …-ranlib).
        ranlib = ar[:-2] + "ranlib" if ar.endswith("ar") else "ranlib"
    return ar, ranlib


def _bundle_basenames(env):
    """All NimBLE-Arduino C/C++ source basenames across every env's libdeps."""
    libdeps = env.subst("$PROJECT_LIBDEPS_DIR")
    names = set()
    if not os.path.isdir(libdeps):
        return names
    for env_dir in os.listdir(libdeps):
        src = os.path.join(libdeps, env_dir, "NimBLE-Arduino", "src", "nimble")
        if not os.path.isdir(src):
            continue
        for root, _dirs, files in os.walk(src):
            for f in files:
                if f.endswith(".c") or f.endswith(".cpp"):
                    names.add(f)
    return names


def _framework_libs_dir(env):
    """Resolve framework-arduinoespressif32-libs by folder name under the
    PlatformIO packages root. We avoid PioPlatform().get_package_dir() because
    the pioarduino platform registers this package under a spec key that differs
    from its on-disk folder name (KeyError)."""
    packages_dir = env.subst("$PROJECT_PACKAGES_DIR") or os.path.expanduser(
        os.path.join("~", ".platformio", "packages")
    )
    cand = os.path.join(packages_dir, "framework-arduinoespressif32-libs")
    return cand if os.path.isdir(cand) else None


def strip_nimble_host(env):
    mcu = env.BoardConfig().get("build.mcu", "esp32c3")
    fw = _framework_libs_dir(env)
    if not fw:
        return
    libbt = os.path.join(fw, mcu, "lib", "libbt.a")
    if not os.path.isfile(libbt):
        return

    ar, ranlib = _ar_ranlib(env)

    try:
        objs = subprocess.check_output([ar, "t", libbt]).decode().split()
    except Exception as exc:  # pragma: no cover - toolchain missing
        print("strip_nimble_host: cannot read %s (%s); skipping" % (libbt, exc))
        return

    if _HOST_MARKER_OBJ not in objs:
        # Already stripped (no host objects present) -> nothing to do.
        return

    bundle = _bundle_basenames(env)
    if not bundle:
        print(
            "strip_nimble_host: NimBLE-Arduino libdep not found yet; "
            "leaving libbt.a intact (will retry next build)"
        )
        return

    # An object is a duplicate iff "<basename>.obj" maps to a bundled source.
    # e.g. "ble_gap.c.obj"[:-4] == "ble_gap.c" which is in the bundle set.
    strip = [o for o in objs if o.endswith(".obj") and o[:-4] in bundle]
    if not strip:
        return

    # Snapshot the pristine archive (overwrites any stale snapshot, since the
    # live archive still contains the host -> it is by definition pristine).
    shutil.copy2(libbt, libbt + ".orig")

    # ar 'd' can delete many members in one call; chunk to stay under arg limits.
    for i in range(0, len(strip), 64):
        subprocess.check_call([ar, "d", libbt] + strip[i : i + 64])
    subprocess.check_call([ranlib, libbt])

    print(
        "strip_nimble_host: removed %d duplicate IDF NimBLE host objects from %s "
        "(backup: libbt.a.orig)" % (len(strip), os.path.relpath(libbt, fw))
    )


strip_nimble_host(env)
