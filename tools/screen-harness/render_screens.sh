#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/screen-harness"
OUT_DIR="${1:-$ROOT_DIR/build/screen-previews}"

mkdir -p "$BUILD_DIR" "$OUT_DIR"

CXX_BIN="${CXX:-g++}"
BIN_PATH="$BUILD_DIR/screen-harness"

pushd "$ROOT_DIR" >/dev/null

"$CXX_BIN" \
  -std=c++20 \
  -O2 \
  -ffunction-sections \
  -fdata-sections \
  -Wl,--gc-sections \
  -DEINK_DISPLAY_SINGLE_BUFFER_MODE=1 \
  -DHOST_BUILD=1 \
  '-DCROSSPOINT_VERSION="screen-harness"' \
  -Itools/screen-harness/stubs \
  -Ilib/hal \
  -Ilib/GfxRenderer \
  -Ilib/EpdFont \
  -Ilib/EpdFont/builtinFonts \
  -Ilib/InflateReader \
  -Ilib/third_party/uzlib/src \
  -Ilib/Utf8 \
  -Ilib/Logging \
  -Ilib/Serialization \
  -Iopen-x4-sdk/libs/display/EInkDisplay/include \
  -Iopen-x4-sdk/libs/hardware/InputManager/include \
  -Iopen-x4-sdk/libs/hardware/BatteryMonitor/include \
  -Isrc \
  tools/screen-harness/main.cpp \
  tools/screen-harness/stubs/stubs.cpp \
  src/activities/Activity.cpp \
  src/activities/boot_sleep/BootActivity.cpp \
  lib/GfxRenderer/GfxRenderer.cpp \
  lib/GfxRenderer/FontCacheManager.cpp \
  lib/EpdFont/EpdFont.cpp \
  lib/EpdFont/EpdFontFamily.cpp \
  lib/EpdFont/FontDecompressor.cpp \
  lib/InflateReader/InflateReader.cpp \
  lib/Utf8/Utf8.cpp \
  lib/Logging/Logging.cpp \
  lib/hal/HalDisplay.cpp \
  open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp \
  -x c lib/third_party/uzlib/src/tinflate.c \
  -o "$BIN_PATH"

"$BIN_PATH" "$OUT_DIR"

echo "Screen previews written to: $OUT_DIR"

popd >/dev/null
