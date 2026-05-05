#!/usr/bin/env bash
# Dev server: builds the host binary and keeps it running with HTML served from disk.
# Edit HTML files and refresh the browser — no rebuild needed.
# Rebuild + restart only when C++ files change (use --watch with entr installed).
#
# Usage:
#   bash test/run_host_server_dev.sh [--host HOST] [--port N] [--root DIR] [--html-root DIR] [--watch]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/host_server"
ARDUINOJSON_DIR="$ROOT_DIR/.pio/libdeps/default/ArduinoJson/src"
BINARY="$BUILD_DIR/HostServer"

PORT=8080
HOST=0.0.0.0
DATA_ROOT="$BUILD_DIR/dev_root"
HTML_ROOT="$ROOT_DIR/src/network/html"
WATCH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) HOST="$2"; shift 2 ;;
    --host=*) HOST="${1#--host=}"; shift ;;
    --port) PORT="$2"; shift 2 ;;
    --port=*) PORT="${1#--port=}"; shift ;;
    --root) DATA_ROOT="$2"; shift 2 ;;
    --root=*) DATA_ROOT="${1#--root=}"; shift ;;
    --html-root) HTML_ROOT="$2"; shift 2 ;;
    --html-root=*) HTML_ROOT="${1#--html-root=}"; shift ;;
    --watch) WATCH=1; shift ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

mkdir -p "$BUILD_DIR" "$DATA_ROOT"

cat >"$BUILD_DIR/HalStorage.h" <<'EOF'
#pragma once
#include "HostStorage.h"
EOF

if ! command -v uv >/dev/null 2>&1; then
  echo "uv is required for host-server dependency bootstrapping" >&2
  exit 1
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "Bootstrapping ArduinoJson..."
  (cd "$ROOT_DIR" && uv run pio pkg install -e default --library "bblanchon/ArduinoJson@7.4.2")
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "ArduinoJson headers not found: $ARDUINOJSON_DIR" >&2
  exit 1
fi

build() {
  echo "Building $BINARY..."
  g++ -std=c++20 -O0 -g -Wno-narrowing \
    -DCROSSPOINT_HOST_BUILD=1 \
    -fno-omit-frame-pointer \
    -pthread \
    -I"$BUILD_DIR" \
    -I"$ROOT_DIR" \
    -I"$ROOT_DIR/test" \
    -I"$ROOT_DIR/test/host_server" \
    -I"$ROOT_DIR/test/mock" \
    -I"$ROOT_DIR/include" \
    -I"$ROOT_DIR/src" \
    -I"$ROOT_DIR/lib/FsHelpers" \
    -I"$ROOT_DIR/lib/third_party/cpp-httplib" \
    -I"$ARDUINOJSON_DIR" \
    "$ROOT_DIR/test/host_server/main.cpp" \
    "$ROOT_DIR/test/host_server/HostWebServer.cpp" \
    "$ROOT_DIR/test/host_server/HostStorage.cpp" \
    "$ROOT_DIR/test/host_server/HostSettingsApi.cpp" \
    "$ROOT_DIR/test/mock/FeatureModuleHooks.cpp" \
    "$ROOT_DIR/src/network/CoreWebRoutes.cpp" \
    "$ROOT_DIR/src/network/FileRoutes.cpp" \
    "$ROOT_DIR/src/network/FileListApi.cpp" \
    "$ROOT_DIR/src/network/FileMutationApi.cpp" \
    "$ROOT_DIR/src/network/FileReadApi.cpp" \
    "$ROOT_DIR/src/network/UploadApi.cpp" \
    "$ROOT_DIR/src/network/SettingsSnapshotApi.cpp" \
    "$ROOT_DIR/src/util/InputValidation.cpp" \
    "$ROOT_DIR/src/util/PathUtils.cpp" \
    "$ROOT_DIR/lib/FsHelpers/FsHelpers.cpp" \
    -o "$BINARY"
  echo "Build OK."
}

build

echo ""
echo "  CrossPoint Host Dev Server"
echo "  http://$HOST:$PORT/"
echo "  http://$HOST:$PORT/files"
echo "  http://$HOST:$PORT/settings"
echo ""
echo "  Files root : $DATA_ROOT"
echo "  HTML root  : $HTML_ROOT"
echo "  (edit HTML and refresh — no rebuild needed)"
echo ""

if [ "$WATCH" -eq 1 ]; then
  if ! command -v entr >/dev/null 2>&1; then
    echo "warning: --watch requires entr (apt install entr / brew install entr)" >&2
    echo "Falling back to single run." >&2
    WATCH=0
  fi
fi

if [ "$WATCH" -eq 1 ]; then
  echo "  --watch active: rebuilds and restarts on C++ changes (Ctrl+C to stop)"
  echo ""
  run_server() {
    exec "$BINARY" --port "$PORT" --root "$DATA_ROOT" --html-root "$HTML_ROOT"
  }
  export -f run_server build
  export BINARY PORT DATA_ROOT HTML_ROOT
  find "$ROOT_DIR/test/host_server" "$ROOT_DIR/src/network" "$ROOT_DIR/src/util" \
       "$ROOT_DIR/lib/FsHelpers" \
    -name "*.cpp" -o -name "*.h" | \
    entr -r bash -c 'build && exec "$BINARY" --host "$HOST" --port "$PORT" --root "$DATA_ROOT" --html-root "$HTML_ROOT"'
else
  exec "$BINARY" --host "$HOST" --port "$PORT" --root "$DATA_ROOT" --html-root "$HTML_ROOT"
fi
