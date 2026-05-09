"""
Recovery shim size budget enforcement.

Runs as a PlatformIO post-build step. Fails the build if the produced
firmware.bin exceeds the budget. The budget exists because the shim
must fit in the 256 KB factory partition with headroom for ESP32
image padding and future growth.

Tunable via env var RECOVERY_SHIM_MAX_BYTES (default 245760 = 240 KB).
"""

import os
import sys

Import("env")  # noqa: F821  — PlatformIO SCons global

DEFAULT_MAX_BYTES = 240 * 1024  # 240 KB
HARD_PARTITION_BYTES = 256 * 1024  # factory partition is 256 KB

max_bytes = int(os.environ.get("RECOVERY_SHIM_MAX_BYTES", DEFAULT_MAX_BYTES))


def check_size(source, target, env):  # noqa: ARG001 — SCons signature
    bin_path = str(target[0])
    try:
        size = os.path.getsize(bin_path)
    except OSError as e:
        print(f"[recovery size-check] could not stat {bin_path}: {e}",
              file=sys.stderr)
        env.Exit(1)
        return

    headroom = max_bytes - size
    pct = (size / HARD_PARTITION_BYTES) * 100
    print(
        f"[recovery size-check] {bin_path}: "
        f"{size} B ({pct:.1f}% of 256 KB factory partition); "
        f"budget {max_bytes} B; headroom {headroom} B"
    )

    if size > max_bytes:
        print(
            f"[recovery size-check] FAIL: shim exceeds {max_bytes} B budget "
            f"by {size - max_bytes} B. Strip code or raise "
            f"RECOVERY_SHIM_MAX_BYTES (current cap is the 256 KB partition).",
            file=sys.stderr,
        )
        env.Exit(1)
        return

    if size > HARD_PARTITION_BYTES:
        # Defense in depth — should be unreachable while max_bytes < HARD.
        print(
            f"[recovery size-check] FATAL: shim larger than factory "
            f"partition ({size} > {HARD_PARTITION_BYTES}). Cannot flash.",
            file=sys.stderr,
        )
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/firmware.bin", check_size)  # noqa: F821
