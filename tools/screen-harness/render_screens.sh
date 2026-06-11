#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/screen-harness"
OUT_DIR="${1:-$ROOT_DIR/build/screen-previews}"
SETTINGS_JSON="${2:-${SCREEN_PREVIEW_SETTINGS_JSON:-}}"
ARDUINOJSON_DIR="$ROOT_DIR/.pio/libdeps/default/ArduinoJson/src"
if [[ ! -d "$ARDUINOJSON_DIR" ]]; then
  echo "Bootstrapping ArduinoJson for screen-harness..."
  (cd "$ROOT_DIR" && uv run pio pkg install -e default --library "bblanchon/ArduinoJson@7.4.2")
fi
JSON_INCLUDE=(-I"$ARDUINOJSON_DIR")

mkdir -p "$BUILD_DIR" "$OUT_DIR"

CXX_BIN="${CXX:-g++}"
BIN_PATH="$BUILD_DIR/screen-harness"
VERIFY_BIN="$BUILD_DIR/verify-pokemon-sprites"
DEVICE="${SCREEN_PREVIEW_DEVICE:-x4}"
DEVICE_FLAGS=()
if [[ "$DEVICE" == "x3" || "$DEVICE" == "X3" ]]; then
  DEVICE_FLAGS+=("-DSCREEN_HARNESS_X3=1")
  BIN_PATH="$BUILD_DIR/screen-harness-x3"
fi

pushd "$ROOT_DIR" >/dev/null

"$CXX_BIN" \
  -std=c++20 \
  -O2 \
  -ffunction-sections \
  -fdata-sections \
  -Wl,--gc-sections \
  -DEINK_DISPLAY_SINGLE_BUFFER_MODE=1 \
  -DHOST_BUILD=1 \
  -DENABLE_POKEMON_PARTY=1 \
  '-DCROSSPOINT_VERSION="screen-harness"' \
  "${DEVICE_FLAGS[@]}" \
  -Itools/screen-harness \
  -Itools/screen-harness/stubs \
  -Iinclude \
  -Ilib/hal \
  -Ilib/GfxRenderer \
  -Ilib/EpdFont \
  -Ilib/EpdFont/builtinFonts \
  -Ilib/I18n \
  -Ilib/InflateReader \
  -Ilib/third_party/uzlib/src \
  -Ilib/Utf8 \
  -Ilib/Logging \
  -Ilib/Memory \
  -Ilib/Epub \
  -Ilib/Serialization \
  -Iopen-x4-sdk/libs/display/EInkDisplay/include \
  -Iopen-x4-sdk/libs/hardware/InputManager/include \
  -Iopen-x4-sdk/libs/hardware/BatteryMonitor/include \
  -Isrc \
  "${JSON_INCLUDE[@]}" \
  tools/screen-harness/main.cpp \
  tools/screen-harness/device_fs_data.cpp \
  tools/screen-harness/stubs/HalStorage.cpp \
  tools/screen-harness/stubs/stubs.cpp \
  src/util/PokemonProgress.cpp \
  src/util/PokemonTeamStore.cpp \
  src/util/PokemonBookDataStore.cpp \
  src/util/RecentBooksStore.cpp \
  tools/screen-harness/stubs/JsonSettingsIO.cpp \
  tools/screen-harness/stubs/BookMetadataCache.cpp \
  src/components/UITheme.cpp \
  src/components/themes/BaseTheme.cpp \
  src/components/themes/minimal/MinimalTheme.cpp \
  src/components/themes/terminal/TerminalTheme.cpp \
  src/components/themes/lyra/LyraTheme.cpp \
  src/components/themes/lyra/Lyra3CoversTheme.cpp \
  src/components/themes/lyra/ForkDriftTheme.cpp \
  src/components/themes/pokemon/PokemonPartyTheme.cpp \
  src/components/themes/lyra/LyraCarouselTheme.cpp \
  src/activities/Activity.cpp \
  src/activities/ActivityWithSubactivity.cpp \
  src/activities/boot_sleep/BootActivity.cpp \
  src/activities/settings/FactoryResetActivity.cpp \
  lib/GfxRenderer/GfxRenderer.cpp \
  lib/GfxRenderer/Bitmap.cpp \
  lib/GfxRenderer/BitmapHelpers.cpp \
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

"$CXX_BIN" \
  -std=c++20 \
  -O2 \
  -ffunction-sections \
  -fdata-sections \
  -Wl,--gc-sections \
  -DHOST_BUILD=1 \
  -DENABLE_POKEMON_PARTY=1 \
  "${DEVICE_FLAGS[@]}" \
  -Itools/screen-harness \
  -Itools/screen-harness/stubs \
  -Iinclude \
  -Ilib/hal \
  -Ilib/GfxRenderer \
  -Ilib/Logging \
  -Ilib/Memory \
  -Ilib/Epub \
  -Ilib/Serialization \
  -Isrc \
  "${JSON_INCLUDE[@]}" \
  tools/screen-harness/verify_pokemon_sprites.cpp \
  tools/screen-harness/device_fs_data.cpp \
  tools/screen-harness/stubs/HalStorage.cpp \
  tools/screen-harness/stubs/verify_stubs.cpp \
  src/util/PokemonProgress.cpp \
  src/util/PokemonTeamStore.cpp \
  src/util/PokemonBookDataStore.cpp \
  src/util/RecentBooksStore.cpp \
  src/util/PokemonPartySprites.cpp \
  tools/screen-harness/stubs/JsonSettingsIO.cpp \
  tools/screen-harness/stubs/BookMetadataCache.cpp \
  lib/GfxRenderer/Bitmap.cpp \
  lib/GfxRenderer/BitmapHelpers.cpp \
  lib/Logging/Logging.cpp \
  -o "$VERIFY_BIN"

if [[ -n "$SETTINGS_JSON" ]]; then
  "$BIN_PATH" "$OUT_DIR" "$SETTINGS_JSON"
else
  "$BIN_PATH" "$OUT_DIR"
fi

if [[ -d "$ROOT_DIR/../deviceFilesystem/.crosspoint" || -n "${SCREEN_HARNESS_SD_ROOT:-}" ]]; then
  echo "Verifying Pokemon sprite loading against device snapshot..."
  "$VERIFY_BIN"
fi

echo "Screen previews written to: $OUT_DIR"

popd >/dev/null
