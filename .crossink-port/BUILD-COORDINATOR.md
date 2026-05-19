# CrossInk port build coordinator

Entry point: `./.crossink-port/port-build.sh` (symlink: `scripts/port-build.sh`).

Never run `uv run pio run` directly during port work. One coordinator run per tree fingerprint.

See script for fingerprinting, `flock` lock (`.build.lock`), cache (`.last-build`), and exit codes:
0 ok/cached, 1 build fail, 2 lock timeout, 3 setup error.
