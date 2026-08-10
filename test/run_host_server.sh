#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/host_server"
ARDUINOJSON_DIR="$ROOT_DIR/.pio/libdeps/default/ArduinoJson/src"
BINARY="$BUILD_DIR/HostServer"

mkdir -p "$BUILD_DIR"

cat >"$BUILD_DIR/HalStorage.h" <<'EOF'
#pragma once
#include "HostStorage.h"
EOF

if ! command -v uv >/dev/null 2>&1; then
  echo "uv is required for host-server dependency bootstrapping" >&2
  exit 1
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "Bootstrapping ArduinoJson for host server..."
  (
    cd "$ROOT_DIR"
    uv run pio pkg install -e default --library "bblanchon/ArduinoJson@7.4.2"
  )
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "ArduinoJson headers not found: $ARDUINOJSON_DIR" >&2
  exit 1
fi

g++ -std=c++20 -O2 -Wno-narrowing \
  -DCROSSPOINT_HOST_BUILD=1 \
  -fsanitize=address,undefined \
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
  "$ROOT_DIR/src/network/server/CoreWebRoutes.cpp" \
  "$ROOT_DIR/src/network/server/FileListApi.cpp" \
  "$ROOT_DIR/src/network/server/DirScan.cpp" \
  "$ROOT_DIR/src/network/server/FileMutationApi.cpp" \
  "$ROOT_DIR/src/network/server/FileRoutes.cpp" \
  "$ROOT_DIR/src/network/server/FileReadApi.cpp" \
  "$ROOT_DIR/src/network/server/UploadApi.cpp" \
  "$ROOT_DIR/src/network/http/BufferedHttpUpload.cpp" \
  "$ROOT_DIR/src/network/server/SettingsSnapshotApi.cpp" \
  "$ROOT_DIR/src/util/InputValidation.cpp" \
  "$ROOT_DIR/src/util/PathUtils.cpp" \
  "$ROOT_DIR/lib/FsHelpers/FsHelpers.cpp" \
  -o "$BINARY"

PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"

SERVER_PID=""
TMP_ROOT="$(mktemp -d)"
cleanup() {
  if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID"
    wait "$SERVER_PID" || true
  fi
  rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

touch "$TMP_ROOT/hello.txt"
mkdir -p "$TMP_ROOT/.crosspoint"
cat >"$TMP_ROOT/.crosspoint/recent.json" <<'JSON'
{"books":[{"path":"/hello.txt","title":"Hello","author":"Host","coverBmpPath":""},{"path":"/book-1.txt","title":"Book 1","author":"Host","coverBmpPath":""},{"path":"/book-2.txt","title":"Book 2","author":"Host","coverBmpPath":""},{"path":"/book-3.txt","title":"Book 3","author":"Host","coverBmpPath":""},{"path":"/book-4.txt","title":"Book 4","author":"Host","coverBmpPath":""},{"path":"/book-5.txt","title":"Book 5","author":"Host","coverBmpPath":""},{"path":"/book-6.txt","title":"Book 6","author":"Host","coverBmpPath":""},{"path":"/book-7.txt","title":"Book 7","author":"Host","coverBmpPath":""},{"path":"/book-8.txt","title":"Book 8","author":"Host","coverBmpPath":""},{"path":"/book-9.txt","title":"Book 9","author":"Host","coverBmpPath":""},{"path":"/book-10.txt","title":"Book 10","author":"Host","coverBmpPath":""},{"path":"/book-11.txt","title":"Book 11","author":"Host","coverBmpPath":""}]}
JSON

"$BINARY" --port "$PORT" --root "$TMP_ROOT" >"$BUILD_DIR/server.log" 2>&1 &
SERVER_PID="$!"

for _ in $(seq 1 50); do
  if curl -fsS "http://127.0.0.1:$PORT/api/settings/raw" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

HEADER_FILE="$BUILD_DIR/headers.txt"
BODY_FILE="$BUILD_DIR/body.json"
curl -fsS -D "$HEADER_FILE" -o "$BODY_FILE" "http://127.0.0.1:$PORT/api/settings/raw" >/dev/null

grep -q "^HTTP/.* 200" "$HEADER_FILE"
grep -qi "^Content-Type: application/json" "$HEADER_FILE"

curl -fsS -D "$HEADER_FILE" -o "$BODY_FILE" "http://127.0.0.1:$PORT/api/settings" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
grep -qi "^Content-Type: application/json" "$HEADER_FILE"
grep -q '"key":"sleepScreen"' "$BODY_FILE"

curl -fsS -X POST -H "Content-Type: application/json" -D "$HEADER_FILE" -o "$BUILD_DIR/settings-post.txt" \
  --data '{"sleepScreen":3,"deviceName":"host-test"}' \
  "http://127.0.0.1:$PORT/api/settings" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
grep -q '"sleepScreen":3' "$TMP_ROOT/settings.json"
grep -q '"deviceName":"host-test"' "$TMP_ROOT/settings.json"

curl -fsS -D "$HEADER_FILE" -o "$BODY_FILE" "http://127.0.0.1:$PORT/api/recent" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
grep -qi "^Content-Type: application/json" "$HEADER_FILE"
grep -q '"path":"/hello.txt"' "$BODY_FILE"
grep -q '"hasCover":false' "$BODY_FILE"
grep -q '"path":"/book-9.txt"' "$BODY_FILE"
! grep -q '"path":"/book-10.txt"' "$BODY_FILE"

curl -fsS -D "$HEADER_FILE" -o "$BODY_FILE" "http://127.0.0.1:$PORT/api/files?path=/" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
grep -qi "^Content-Type: application/json" "$HEADER_FILE"
grep -q '"hello.txt"' "$BODY_FILE"

curl -sS -X POST -D "$HEADER_FILE" -o "$BUILD_DIR/mkdir-legacy.txt" \
  "http://127.0.0.1:$PORT/mkdir?name=newdir&path=/" >/dev/null
grep -q "^HTTP/.* 400" "$HEADER_FILE"
grep -q "Use JSON name/path body" "$BUILD_DIR/mkdir-legacy.txt"

curl -fsS -X POST -H "Content-Type: application/json" -D "$HEADER_FILE" -o "$BUILD_DIR/mkdir.txt" \
  --data '{"name":"newdir","path":"/"}' \
  "http://127.0.0.1:$PORT/mkdir" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
[ -d "$TMP_ROOT/newdir" ]

curl -sS -X POST -D "$HEADER_FILE" -o "$BUILD_DIR/rename-legacy.txt" \
  "http://127.0.0.1:$PORT/rename?path=%2Fhello.txt&name=renamed.txt" >/dev/null
grep -q "^HTTP/.* 400" "$HEADER_FILE"
grep -q "Use JSON from/to body" "$BUILD_DIR/rename-legacy.txt"

curl -fsS -X POST -H "Content-Type: application/json" -D "$HEADER_FILE" -o "$BUILD_DIR/rename.txt" \
  --data '{"from":"/hello.txt","to":"renamed.txt"}' \
  "http://127.0.0.1:$PORT/rename" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
[ ! -e "$TMP_ROOT/hello.txt" ]
[ -e "$TMP_ROOT/renamed.txt" ]

curl -sS -X POST -D "$HEADER_FILE" -o "$BUILD_DIR/move-legacy.txt" \
  "http://127.0.0.1:$PORT/move?path=%2Frenamed.txt&dest=%2Fnewdir" >/dev/null
grep -q "^HTTP/.* 400" "$HEADER_FILE"
grep -q "Use JSON from/to body" "$BUILD_DIR/move-legacy.txt"

curl -fsS -X POST -H "Content-Type: application/json" -D "$HEADER_FILE" -o "$BUILD_DIR/move.txt" \
  --data '{"from":"/renamed.txt","to":"/newdir"}' \
  "http://127.0.0.1:$PORT/move" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
[ ! -e "$TMP_ROOT/renamed.txt" ]
[ -e "$TMP_ROOT/newdir/renamed.txt" ]

curl -sS -X POST -D "$HEADER_FILE" -o "$BUILD_DIR/delete-legacy.txt" \
  "http://127.0.0.1:$PORT/delete?path=%2Fnewdir%2Frenamed.txt" >/dev/null
grep -q "^HTTP/.* 400" "$HEADER_FILE"
grep -q "Use paths JSON array" "$BUILD_DIR/delete-legacy.txt"

curl -sS -X POST -D "$HEADER_FILE" -o "$BUILD_DIR/delete-paths-legacy.txt" \
  "http://127.0.0.1:$PORT/delete?paths=%5B%22%2Fnewdir%2Frenamed.txt%22%5D" >/dev/null
grep -q "^HTTP/.* 400" "$HEADER_FILE"
grep -q "Use paths JSON array" "$BUILD_DIR/delete-paths-legacy.txt"

curl -fsS -X POST -H "Content-Type: application/json" -D "$HEADER_FILE" -o "$BUILD_DIR/delete.txt" \
  --data '["/newdir/renamed.txt"]' \
  "http://127.0.0.1:$PORT/delete" >/dev/null
grep -q "^HTTP/.* 200" "$HEADER_FILE"
[ ! -e "$TMP_ROOT/newdir/renamed.txt" ]
