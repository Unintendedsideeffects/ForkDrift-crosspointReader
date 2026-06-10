#!/bin/bash
# Serialized PlatformIO runner. ALL pio builds (manual, agent-driven, CI-local)
# should go through this wrapper instead of calling `uv run pio` directly.
#
# Why: concurrent pio runs corrupt shared state in two places —
#   1. the shared build dir (platformio.local.ini points all checkouts and
#      worktrees at ~/.cache/crosspoint-pio-build), where parallel SCons
#      processes clobber each other's .o files, and
#   2. ~/.platformio's global package cache, where parallel package installs
#      race the VCS clone/unpack steps and leave half-installed libdeps.
#
# The pre-commit hook takes the same lock around its verification build, so a
# wrapped build queues behind an in-flight hook build (and vice versa) instead
# of corrupting it.
#
# Usage: scripts/pio-locked.sh run -e default
#        PIO_LOCK_TIMEOUT=600 scripts/pio-locked.sh run -e custom

set -euo pipefail

LOCKFILE=/tmp/crosspoint-pio-build.lock
TIMEOUT="${PIO_LOCK_TIMEOUT:-3600}"

exec 9>"$LOCKFILE"
if ! flock -n 9; then
    echo "pio-locked: waiting for in-flight pio build (lock: $LOCKFILE, timeout ${TIMEOUT}s)..." >&2
    if ! flock -w "$TIMEOUT" 9; then
        echo "pio-locked: ERROR: timed out after ${TIMEOUT}s waiting for $LOCKFILE" >&2
        exit 1
    fi
fi

exec uv run pio "$@"
