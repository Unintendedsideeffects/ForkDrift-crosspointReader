#!/usr/bin/env bash
# Seed a delegated agent's worktree/clone with this machine's build configuration.
#
# WHY THIS EXISTS
# ---------------
# platformio.local.ini is gitignored, so a fresh worktree or clone does not have it.
# Without it PlatformIO falls back to its defaults and writes BOTH the build output
# and its SCons cache *inside the tree*. The SCons cache is a hash-bucketed mirror of
# every object the toolchain has ever produced, so each agent tree grows a private
# copy — measured at 3.7 GB per tree, 19 GB across 22 stale trees, which filled the
# root filesystem to 0 bytes free on 2026-08-26.
#
# What this seeds instead:
#   build_dir       -> a per-tree subdir under ~/.cache/crosspoint-pio-build
#                      Separate per tree, because two source trees sharing one build
#                      dir force a full rebuild every time you switch between them.
#                      scripts/hooks/pre-commit already prunes subdirs here after 14
#                      days, so these expire on their own.
#   build_cache_dir -> the SHARED ~/.cache/crosspoint-pio-cache
#                      Safe to share: the SCons cache is content-addressed (00..FF
#                      buckets keyed by content hash), so trees cooperate rather than
#                      collide, and identical objects are stored once instead of N
#                      times. This is the line that stops the disk blowup.
#
# Usage:
#   scripts/seed-agent-tree.sh /abs/path/to/agent-tree [--link-libdeps] [--no-hooks]
#
#   --link-libdeps  Symlink .pio from this checkout so a cold tree does not re-run
#                   `pio pkg install` against the shared ~/.platformio package cache.
#                   libdeps headers are read-only, so concurrent reads are safe.
#   --no-hooks      Point core.hooksPath at an empty dir. Delegated agents are told
#                   not to commit, and a pre-commit build is expensive if they try.
#
# Run this immediately after `git worktree add` / `git clone`, before dispatching.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_CHECKOUT="$(cd "$SCRIPT_DIR/.." && pwd)"

TARGET=""
LINK_LIBDEPS=0
NO_HOOKS=0

while [ $# -gt 0 ]; do
  case "$1" in
    --link-libdeps) LINK_LIBDEPS=1 ;;
    --no-hooks)     NO_HOOKS=1 ;;
    -h|--help)      sed -n '2,36p' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*)             echo "unknown option: $1" >&2; exit 2 ;;
    *)              [ -n "$TARGET" ] && { echo "unexpected argument: $1" >&2; exit 2; }
                    TARGET="$1" ;;
  esac
  shift
done

if [ -z "$TARGET" ]; then
  echo "usage: $(basename "$0") /abs/path/to/agent-tree [--link-libdeps] [--no-hooks]" >&2
  exit 2
fi

if [ ! -d "$TARGET" ]; then
  echo "error: target is not a directory: $TARGET" >&2
  exit 1
fi

TARGET="$(cd "$TARGET" && pwd)"

if [ "$TARGET" = "$MAIN_CHECKOUT" ]; then
  echo "error: refusing to seed the main checkout ($MAIN_CHECKOUT)" >&2
  echo "       this script is for delegated agent trees only" >&2
  exit 1
fi

if [ ! -f "$TARGET/platformio.ini" ]; then
  echo "error: no platformio.ini at $TARGET — is this a crosspoint-reader tree?" >&2
  exit 1
fi

# A stable, filesystem-safe id for this tree so its build dir is its own.
# printf (not echo/basename directly) so basename's trailing newline is not itself
# translated into a '-', which would make every slug end in a stray dash.
SLUG="$(printf '%s' "$(basename "$TARGET")" | tr -cs 'A-Za-z0-9_.-' '-')"
BUILD_DIR="$HOME/.cache/crosspoint-pio-build/$SLUG"
CACHE_DIR="$HOME/.cache/crosspoint-pio-cache"

if [ -e "$TARGET/platformio.local.ini" ]; then
  echo "note: platformio.local.ini already present, leaving it alone:"
  sed 's/^/      /' "$TARGET/platformio.local.ini"
else
  cat > "$TARGET/platformio.local.ini" <<EOF
[platformio]
; Seeded by scripts/seed-agent-tree.sh — do not commit (gitignored).
; Per-tree build output: keeps trees from forcing each other into full rebuilds.
; Pruned after 14 days by scripts/hooks/pre-commit.
build_dir = $BUILD_DIR
; SHARED content-addressed SCons cache. Sharing is the point: an unseeded tree
; writes its own multi-GB copy inside itself instead.
build_cache_dir = $CACHE_DIR
EOF
  mkdir -p "$BUILD_DIR" "$CACHE_DIR"
  echo "seeded: $TARGET/platformio.local.ini"
  echo "        build_dir       = $BUILD_DIR"
  echo "        build_cache_dir = $CACHE_DIR  (shared)"
fi

if [ "$LINK_LIBDEPS" -eq 1 ]; then
  if [ -e "$TARGET/.pio" ]; then
    echo "note: $TARGET/.pio already exists, not replacing it"
  elif [ -d "$MAIN_CHECKOUT/.pio" ]; then
    ln -sfn "$MAIN_CHECKOUT/.pio" "$TARGET/.pio"
    echo "linked: .pio -> $MAIN_CHECKOUT/.pio (libdeps reused, read-only)"
  else
    echo "warn:  $MAIN_CHECKOUT/.pio does not exist — nothing to link" >&2
  fi
fi

if [ "$NO_HOOKS" -eq 1 ]; then
  EMPTY_HOOKS="$HOME/.cache/crosspoint-agent-no-hooks"
  mkdir -p "$EMPTY_HOOKS"
  git -C "$TARGET" config core.hooksPath "$EMPTY_HOOKS"
  echo "hooks:  neutralized (core.hooksPath = $EMPTY_HOOKS)"
fi

# Submodules are not inherited by a worktree, and git rejects a symlinked submodule
# path outright ("expected submodule path 'open-x4-sdk' not to be a symbolic link"),
# so the only correct fix is a real init. Cloning is cheap here: it resolves against
# the local object store.
if [ -f "$TARGET/.gitmodules" ] && [ ! -e "$TARGET/open-x4-sdk/libs" ]; then
  echo "note:  open-x4-sdk submodule is not populated. Run:"
  echo "         git -C $TARGET submodule update --init --recursive"
fi

echo "done."
