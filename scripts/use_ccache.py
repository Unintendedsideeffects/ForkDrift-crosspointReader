"""Enable ccache for the PlatformIO build if `ccache` is installed.

Wraps the toolchain's CC/CXX with `ccache`, which caches compiler output keyed
by the hash of the preprocessed source. Survives mtime changes from `git
stash`, `git checkout`, etc., that defeat SCons's mtime-based incremental
detection. Typical speedup on a no-op rebuild after `git stash pop`: 5 min ->
10-30 s.

Sets max_size=2G on every build (idempotent); ccache handles LRU eviction
within that cap. The SCons CacheDir (crosspoint-pio-cache) is separate and
pruned by the pre-commit hook.
"""

import subprocess
from shutil import which

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

CCACHE_MAX_SIZE = "2G"

ccache_bin = which("ccache")
if ccache_bin:
    # Persisted in ~/.config/ccache/ccache.conf; idempotent if unchanged.
    subprocess.run([ccache_bin, "--set-config", f"max_size={CCACHE_MAX_SIZE}"], check=False)

    env.Replace(  # noqa: F821
        CC=f"{ccache_bin} " + env["CC"],  # noqa: F821
        CXX=f"{ccache_bin} " + env["CXX"],  # noqa: F821
    )
    print(f"ccache: enabled ({ccache_bin}, cap={CCACHE_MAX_SIZE})")
else:
    print("ccache: not installed; builds will use SCons mtime caching only")
