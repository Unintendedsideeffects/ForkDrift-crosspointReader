"""Enable ccache for the PlatformIO build if `ccache` is installed.

Wraps the toolchain's CC/CXX with `ccache`, which caches compiler output keyed
by the hash of the preprocessed source. Survives mtime changes from `git
stash`, `git checkout`, etc., that defeat SCons's mtime-based incremental
detection. Typical speedup on a no-op rebuild after `git stash pop`: 5 min ->
10-30 s.

Idempotent: configures `max_size=5G` on every build but ccache treats this as
a no-op when already set. ccache itself handles LRU eviction within that cap,
so there is no separate cleanup step for the ccache store.
"""

import subprocess
from shutil import which

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

ccache_bin = which("ccache")
if ccache_bin:
    # Persisted in ~/.config/ccache/ccache.conf; idempotent if unchanged.
    subprocess.run([ccache_bin, "--set-config", "max_size=5G"], check=False)

    env.Replace(  # noqa: F821
        CC=f"{ccache_bin} " + env["CC"],  # noqa: F821
        CXX=f"{ccache_bin} " + env["CXX"],  # noqa: F821
    )
    print(f"ccache: enabled ({ccache_bin}, cap=5G)")
else:
    print("ccache: not installed; builds will use SCons mtime caching only")
