#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/host_tests"
ARDUINOJSON_DIR="$ROOT_DIR/.pio/libdeps/default/ArduinoJson/src"

mkdir -p "$BUILD_DIR"

if ! command -v uv >/dev/null 2>&1; then
  echo "uv is required for host-test dependency bootstrapping" >&2
  exit 1
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "Bootstrapping ArduinoJson for host tests..."
  (
    cd "$ROOT_DIR"
    uv run pio pkg install -e default --library "bblanchon/ArduinoJson@7.4.2"
  )
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "ArduinoJson headers not found: $ARDUINOJSON_DIR" >&2
  echo "Install them with: uv run pio pkg install -e default --library \"bblanchon/ArduinoJson@7.4.2\"" >&2
  exit 1
fi

gcc -c "$ROOT_DIR/lib/third_party/md4c/md4c.c" -I"$ROOT_DIR/lib/third_party/md4c" -o "$BUILD_DIR/md4c.o"
gcc -c "$ROOT_DIR/lib/third_party/md4c/entity.c" -I"$ROOT_DIR/lib/third_party/md4c" -o "$BUILD_DIR/entity.o"

# Enable the web pokedex/pokemon party routes so host tests compile and exercise them.
g++ -std=c++20 -O0 -g -Wno-narrowing \
  -DCROSSPOINT_HOST_BUILD=1 \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  -DENABLE_POKEMON_WALLPAPER_PLUGIN=1 \
  -DENABLE_POKEMON_PARTY=1 \
  -DENABLE_IMAGE_SLEEP=1 \
  -I"$ROOT_DIR" \
  -I"$ROOT_DIR/test" \
  -I"$ROOT_DIR/test/mock" \
  -I"$ROOT_DIR/lib/I18n" \
  -I"$ROOT_DIR/lib/Logging" \
  -I"$ROOT_DIR/lib/FsHelpers" \
  -I"$ROOT_DIR/lib/Markdown" \
  -I"$ROOT_DIR/lib/third_party/md4c" \
  -I"$ROOT_DIR/lib/Serialization" \
  -I"$ROOT_DIR/include" \
  -I"$ROOT_DIR/src" \
  -I"$ARDUINOJSON_DIR" \
  "$ROOT_DIR/test/host/"*.cpp \
  "$ROOT_DIR/src/network/AssetReadApi.cpp" \
  "$ROOT_DIR/src/network/FileListApi.cpp" \
  "$ROOT_DIR/src/network/FileMutationApi.cpp" \
  "$ROOT_DIR/src/network/FileReadApi.cpp" \
  "$ROOT_DIR/lib/FsHelpers/FsHelpers.cpp" \
  "$ROOT_DIR/lib/Logging/Logging.cpp" \
  "$ROOT_DIR/lib/Markdown/MarkdownPreprocessor.cpp" \
  "$ROOT_DIR/lib/Markdown/MarkdownParser.cpp" \
  "$ROOT_DIR/src/core/features/FeatureCatalog.cpp" \
  "$ROOT_DIR/src/network/ReadingDataApi.cpp" \
  "$ROOT_DIR/src/network/RemoteControlApi.cpp" \
  "$ROOT_DIR/src/network/SleepCoverApi.cpp" \
  "$ROOT_DIR/src/network/SettingsSnapshotApi.cpp" \
  "$ROOT_DIR/src/network/TodoPlannerApi.cpp" \
  "$ROOT_DIR/src/features/pokemon_party/Registration.cpp" \
  "$ROOT_DIR/src/features/remote_keyboard_input/Registration.cpp" \
  "$ROOT_DIR/src/network/RemoteKeyboardSession.cpp" \
  "$ROOT_DIR/src/network/RecentBookJson.cpp" \
  "$ROOT_DIR/src/util/ForkDriftNavigation.cpp" \
  "$ROOT_DIR/src/util/BookProgressDataStore.cpp" \
  "$ROOT_DIR/src/util/InputValidation.cpp" \
  "$ROOT_DIR/src/util/PathUtils.cpp" \
  "$ROOT_DIR/src/util/PokemonBookDataStore.cpp" \
  "$ROOT_DIR/src/CrossPointSettings.cpp" \
  "$ROOT_DIR/test/mock/FeatureModuleHooks.cpp" \
  "$ROOT_DIR/test/mock/JsonSettingsIO.cpp" \
  "$BUILD_DIR/md4c.o" \
  "$BUILD_DIR/entity.o" \
  -o "$BUILD_DIR/HostTests"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

"$BUILD_DIR/HostTests"
"$BUILD_DIR/HostTests" --reporters=junit --out="$BUILD_DIR/results.xml"
python3 "$ROOT_DIR/scripts/generate_configurator_settings_schema.py" --check
