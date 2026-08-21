#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/host_tests"
ARDUINOJSON_DIR="$ROOT_DIR/.pio/libdeps/default/ArduinoJson/src"
SIMULATOR_MBEDTLS_DIR="$ROOT_DIR/.pio/libdeps/simulator/simulator/src"

mkdir -p "$BUILD_DIR"

if ! command -v uv >/dev/null 2>&1; then
  echo "uv is required for host-test dependency bootstrapping" >&2
  exit 1
fi

echo "Generating build-time sources..."
(
  cd "$ROOT_DIR"
  uv run python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
  uv run python3 scripts/build_html.py
)

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "Bootstrapping ArduinoJson for host tests..."
  (
    cd "$ROOT_DIR"
    # No --library here, deliberately. ArduinoJson is already declared in
    # platformio.ini (`bblanchon/ArduinoJson @ 7.4.2`, [base] and the simulator
    # env), so a bare install of the env's declared dependencies gets it.
    #
    # Passing --library made pio PERSIST the dependency, which rewrites
    # platformio.ini wholesale through ConfigParser rather than as a diff. That
    # dropped every symlink://open-x4-sdk/... entry from [base] lib_deps (next
    # firmware build then fails with "EInkDisplay.h: No such file or
    # directory"), stripped all comments, and inlined values from the gitignored
    # platformio.local.ini into the committed file -- putting a developer's
    # personal build_dir one `git add` away from history. It only fires when
    # .pio/libdeps is absent, i.e. on fresh clones and fresh worktrees.
    # See docs/FINDINGS.md 2026-08-15T14:10Z.
    uv run pio pkg install -e default
  )
fi

if [ ! -d "$ARDUINOJSON_DIR" ]; then
  echo "ArduinoJson headers not found: $ARDUINOJSON_DIR" >&2
  echo "Install them with: uv run pio pkg install -e default" >&2
  exit 1
fi

if [ ! -f "$SIMULATOR_MBEDTLS_DIR/mbedtls/base64.h" ]; then
  echo "Bootstrapping simulator mbedtls stubs for host tests..."
  (
    cd "$ROOT_DIR"
    uv run pio pkg install -e simulator
  )
fi

if [ ! -f "$SIMULATOR_MBEDTLS_DIR/mbedtls/base64.h" ]; then
  echo "mbedtls base64 stub not found: $SIMULATOR_MBEDTLS_DIR/mbedtls/base64.h" >&2
  echo "Install it with: uv run pio pkg install -e simulator" >&2
  exit 1
fi

gcc -c "$ROOT_DIR/lib/third_party/md4c/md4c.c" -I"$ROOT_DIR/lib/third_party/md4c" -o "$BUILD_DIR/md4c.o"
gcc -c "$ROOT_DIR/lib/third_party/md4c/entity.c" -I"$ROOT_DIR/lib/third_party/md4c" -o "$BUILD_DIR/entity.o"
gcc -c "$ROOT_DIR/lib/MiniBidi/minibidi.c" -I"$ROOT_DIR/lib/MiniBidi" -o "$BUILD_DIR/minibidi.o"
gcc -c "$ROOT_DIR/lib/third_party/uzlib/src/tinflate.c" -I"$ROOT_DIR/lib/third_party/uzlib/src" \
  -o "$BUILD_DIR/tinflate.o"
gcc -c "$ROOT_DIR/test/mock/uzlib_checksums.c" -I"$ROOT_DIR/lib/third_party/uzlib/src" -o "$BUILD_DIR/uzlib_checksums.o"
gcc -c "$ROOT_DIR/lib/miniz/src/miniz_impl.c" -I"$ROOT_DIR/lib/miniz/src" -I"$ROOT_DIR/lib/miniz/third_party" \
  -o "$BUILD_DIR/miniz_impl.o"

# Enable the web pokedex/pokemon party routes so host tests compile and exercise them.
# ENABLE_HYPHENATION=0: ParsedText::hyphenateWordAtIndex() short-circuits to `return
# false` when this is 0 (ParsedText.cpp:812-814), so the whole Hyphenator/LanguageRegistry
# trie-table dependency chain never needs to be compiled or linked for host tests -- none
# of the ported epubparse invariants touch hyphenation.
g++ -std=c++20 -O0 -g -Wno-narrowing \
  -ffunction-sections -fdata-sections \
  -DCROSSPOINT_HOST_BUILD=1 \
  -DENABLE_HYPHENATION=0 \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  -DENABLE_TEXT_SELECTION=1 \
  -DENABLE_ANNOTATIONS=1 \
  -DENABLE_BOOKMARKS=1 \
  -DENABLE_POKEMON_PARTY=1 \
  -DENABLE_IMAGE_SLEEP=1 \
  -DENABLE_BACKGROUND_SERVER=1 \
  -DENABLE_BACKGROUND_SERVER_ON_CHARGE=1 \
  -DENABLE_BACKGROUND_SERVER_ALWAYS=1 \
  -DENABLE_TERMINUS_SLEEP=1 \
  -I"$ROOT_DIR" \
  -I"$ROOT_DIR/test" \
  -I"$ROOT_DIR/test/mock" \
  -I"$ROOT_DIR/lib/I18n" \
  -I"$ROOT_DIR/lib/Logging" \
  -I"$ROOT_DIR/lib/FsHelpers" \
  -I"$ROOT_DIR/lib/EpdFont" \
  -I"$ROOT_DIR/lib/Markdown" \
  -I"$ROOT_DIR/lib/Memory" \
  -I"$ROOT_DIR/lib/InflateReader" \
  -I"$ROOT_DIR/lib/miniz/src" \
  -I"$ROOT_DIR/lib/PngToBmpConverter" \
  -I"$ROOT_DIR/lib/third_party/uzlib/src" \
  -I"$ROOT_DIR/lib/third_party/md4c" \
  -I"$ROOT_DIR/lib/Serialization" \
  -I"$ROOT_DIR/lib/GfxRenderer" \
  -I"$ROOT_DIR/lib/OpdsParser" \
  -I"$ROOT_DIR/lib/JsonParser" \
  -I"$ROOT_DIR/lib/XmlParserUtils" \
  -I"$ROOT_DIR/include" \
  -I"$ROOT_DIR/src" \
  -I"$ARDUINOJSON_DIR" \
  -I"$SIMULATOR_MBEDTLS_DIR" \
  -I"$ROOT_DIR/lib/Epub" \
  -I"$ROOT_DIR/lib/Epub/Epub/converters" \
  -I"$ROOT_DIR/lib/Utf8" \
  -I"$ROOT_DIR/lib/MiniBidi" \
  "$ROOT_DIR/test/host/"*.cpp \
  "$ROOT_DIR/lib/OpdsParser/OpenSearchParser.cpp" \
  "$ROOT_DIR/lib/OpdsParser/OpdsParser.cpp" \
  "$ROOT_DIR/lib/JsonParser/StreamingJsonParser.cpp" \
  "$ROOT_DIR/lib/JsonParser/ReleaseJsonParser.cpp" \
  "$ROOT_DIR/src/network/wifi/BleCredentialParser.cpp" \
  "$ROOT_DIR/src/network/server/AssetReadApi.cpp" \
  "$ROOT_DIR/src/network/server/FileListApi.cpp" \
  "$ROOT_DIR/src/network/server/FileMutationApi.cpp" \
  "$ROOT_DIR/src/network/server/FileReadApi.cpp" \
  "$ROOT_DIR/src/network/server/DirScan.cpp" \
  "$ROOT_DIR/src/network/server/WebJsonUtils.cpp" \
  "$ROOT_DIR/lib/FsHelpers/FsHelpers.cpp" \
  "$ROOT_DIR/lib/I18n/I18n.cpp" \
  "$ROOT_DIR/lib/I18n/I18nStrings.cpp" \
  "$ROOT_DIR/lib/Logging/Logging.cpp" \
  "$ROOT_DIR/lib/Memory/HeapGuard.cpp" \
  "$ROOT_DIR/lib/Memory/BuildScratch.cpp" \
  "$ROOT_DIR/lib/InflateReader/InflateReader.cpp" \
  "$ROOT_DIR/lib/miniz/src/InflateStream.cpp" \
  "$ROOT_DIR/lib/PngToBmpConverter/PngToBmpConverter.cpp" \
  "$ROOT_DIR/lib/Epub/Epub/converters/ImageDimsProbe.cpp" \
  "$ROOT_DIR/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp" \
  "$ROOT_DIR/lib/Markdown/MarkdownPreprocessor.cpp" \
  "$ROOT_DIR/lib/Markdown/MarkdownParser.cpp" \
  "$ROOT_DIR/src/core/features/FeatureCatalog.cpp" \
  "$ROOT_DIR/src/core/features/KoreaderOpdsBridge.cpp" \
  "$ROOT_DIR/src/network/server/ReadingDataApi.cpp" \
  "$ROOT_DIR/src/network/server/RemoteControlApi.cpp" \
  "$ROOT_DIR/src/network/server/SleepCoverApi.cpp" \
  "$ROOT_DIR/src/network/server/SettingsSnapshotApi.cpp" \
  "$ROOT_DIR/src/network/server/NotesApi.cpp" \
  "$ROOT_DIR/src/network/server/TodoPlannerApi.cpp" \
  "$ROOT_DIR/src/activities/todo/TodoPlannerStorage.cpp" \
  "$ROOT_DIR/src/util/DateUtils.cpp" \
  "$ROOT_DIR/src/features/pokemon_party/Registration.cpp" \
  "$ROOT_DIR/src/features/remote_keyboard_input/Registration.cpp" \
  "$ROOT_DIR/src/network/server/RemoteKeyboardSession.cpp" \
  "$ROOT_DIR/src/network/server/RecentBookJson.cpp" \
  "$ROOT_DIR/src/util/ForkDriftNavigation.cpp" \
  "$ROOT_DIR/src/util/BookProgressDataStore.cpp" \
  "$ROOT_DIR/src/util/InputValidation.cpp" \
  "$ROOT_DIR/src/util/PathUtils.cpp" \
  "$ROOT_DIR/src/util/TerminusApi.cpp" \
  "$ROOT_DIR/src/features/terminus_sleep/RefreshEvidence.cpp" \
  "$ROOT_DIR/src/util/UrlUtils.cpp" \
  "$ROOT_DIR/src/util/StringUtils.cpp" \
  "$ROOT_DIR/src/util/OpdsFilename.cpp" \
  "$ROOT_DIR/lib/Utf8/Utf8.cpp" \
  "$ROOT_DIR/src/util/PokemonBookDataStore.cpp" \
  "$ROOT_DIR/src/util/PokemonProgress.cpp" \
  "$ROOT_DIR/src/util/PokemonPartySprites.cpp" \
  "$ROOT_DIR/src/util/PokemonTeamStore.cpp" \
  "$ROOT_DIR/src/util/RecentBooksStore.cpp" \
  "$ROOT_DIR/src/util/FlashcardsStore.cpp" \
  "$ROOT_DIR/src/util/DictionaryLookup.cpp" \
  "$ROOT_DIR/src/CrossPointSettings.cpp" \
  "$ROOT_DIR/src/util/SettingsBackup.cpp" \
  "$ROOT_DIR/src/util/BookSettingsOverride.cpp" \
  "$ROOT_DIR/src/util/AnnotationStore.cpp" \
  "$ROOT_DIR/src/util/HighlightExporter.cpp" \
  "$ROOT_DIR/lib/MiniBidi/BidiUtils.cpp" \
  "$ROOT_DIR/lib/Epub/Epub/Page.cpp" \
  "$ROOT_DIR/lib/Epub/Epub/blocks/TextBlock.cpp" \
  "$ROOT_DIR/lib/Epub/Epub/blocks/ImageBlockSerialization.cpp" \
  "$ROOT_DIR/src/network/background/BackgroundServerPolicy.cpp" \
  "$ROOT_DIR/src/network/wifi/WifiEntryPolicy.cpp" \
  "$ROOT_DIR/src/util/WifiCredentialStore.cpp" \
  "$ROOT_DIR/test/mock/WifiCredentialStoreDeps.cpp" \
  "$ROOT_DIR/src/BookmarkStore.cpp" \
  "$ROOT_DIR/src/OpdsServerStore.cpp" \
  "$ROOT_DIR/lib/Xtc/Xtc/XtcParser.cpp" \
  "$ROOT_DIR/test/mock/FeatureModuleHooks.cpp" \
  "$ROOT_DIR/test/mock/PokemonPartyStores.cpp" \
  "$ROOT_DIR/src/SettingsSerializer.cpp" \
  "$ROOT_DIR/test/mock/JsonSettingsIO.cpp" \
  "$ROOT_DIR/test/mock/TerminusCredentialStoreMock.cpp" \
  "$ROOT_DIR/lib/GfxRenderer/Bitmap.cpp" \
  "$ROOT_DIR/lib/GfxRenderer/BitmapHelpers.cpp" \
  "$ROOT_DIR/src/util/FirmwareUpdateHelpers.cpp" \
  "$ROOT_DIR/src/network/ota/SerialOtaSession.cpp" \
  "$ROOT_DIR/src/network/server/SettingsApply.cpp" \
  "$ROOT_DIR/src/activities/reader/ReadingStatsStore.cpp" \
  "$ROOT_DIR/src/activities/reader/ReadingStatsAnalytics.cpp" \
  "$ROOT_DIR/src/activities/reader/BookReadingStats.cpp" \
  "$ROOT_DIR/src/activities/reader/GlobalReadingStats.cpp" \
  "$ROOT_DIR/src/activities/reader/SelectionModel.cpp" \
  "$ROOT_DIR/src/activities/reader/SelectionCapturePolicy.cpp" \
  "$ROOT_DIR/src/util/NotesStore.cpp" \
  "$ROOT_DIR/src/activities/home/HomeCarouselCache.cpp" \
  "$ROOT_DIR/lib/Epub/Epub/ParsedText.cpp" \
  "$ROOT_DIR/test/mock/GfxRendererTestStub.cpp" \
  "$BUILD_DIR/md4c.o" \
  "$BUILD_DIR/entity.o" \
  "$BUILD_DIR/minibidi.o" \
  "$BUILD_DIR/tinflate.o" \
  "$BUILD_DIR/uzlib_checksums.o" \
  "$BUILD_DIR/miniz_impl.o" \
  -Wl,--gc-sections \
  -lexpat \
  -o "$BUILD_DIR/HostTests"

export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

"$BUILD_DIR/HostTests"
"$BUILD_DIR/HostTests" --reporters=junit --out="$BUILD_DIR/results.xml"
python3 "$ROOT_DIR/scripts/generate_configurator_settings_schema.py" --check
