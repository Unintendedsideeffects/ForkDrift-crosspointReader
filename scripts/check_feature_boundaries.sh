#!/bin/bash

# Feature boundary enforcement — T003/T045.
#
# Finds ENABLE_* preprocessor guards in app shell code (src/) that should
# instead be in src/features/<name>/Registration.cpp or src/core/.
#
# APPROVED paths (may contain ENABLE_ guards freely):
#   src/core/       — registry and bootstrap implementation
#   src/features/   — per-feature registration units
#
# Exclusions fall into two categories.  Do NOT add new entries without
# assigning them to the correct category and justifying the classification.
#
# ─────────────────────────────────────────────────────────────────────────────
# CATEGORY 1 — PERMANENT compile-time exceptions
#
# These files contain ENABLE_* guards that CANNOT be moved to Registration.cpp
# because the guarded symbols (EpdFont objects, decoder headers, peripheral
# driver classes) are absent from the binary when the feature is disabled.
# A runtime FeatureCatalog / registry check cannot substitute for a symbol
# that is not linked.
# ─────────────────────────────────────────────────────────────────────────────
#
#   CrossPointSettings.cpp  — font/theme OBJECT SELECTION: if ENABLE_BOOKERLY_FONTS=0
#                             the EpdFont objects do not exist in the binary at all.
#                             Same applies to ENABLE_LYRA_THEME theme instances.
#                             A runtime FeatureCatalog check cannot substitute.
#
#   SleepActivity.cpp       — ENABLE_IMAGE_SLEEP selects the extension list; PNG/JPEG
#                             decoders are not linked when disabled.
#
#   OpdsBookBrowserActivity.cpp — guards #include <Epub.h>; the header does not exist
#                                 when ENABLE_EPUB_SUPPORT=0.
#
#   CrossPointWebServer.cpp — ENABLE_IMAGE_SLEEP selects allowed sleep image extensions;
#                             same decoder availability constraint as SleepActivity.
#
#   OtaWebCheck.cpp         — whole-subsystem guard: OtaUpdater and the FreeRTOS check
#                             task are not compiled when ENABLE_OTA_UPDATES=0. The
#                             compile-time guard cannot be replaced by a runtime registry
#                             check because the dependent symbols do not exist.
#
#   UserFontManager.*       — peripheral driver; compile-time option.
#   BleWifiProvisioner.*    — peripheral driver; compile-time option.
#
#   SettingsList.h          — feature-owned setting fields and translation keys are
#                             compiled out with annotations/bookmarks.
#
#   EpubReaderActivity.* / EpubReaderMenuActivity.*
#                           — feature-owned enum members, method declarations, model
#                             fields, and persistence symbols do not exist in disabled
#                             builds, so runtime registration cannot replace the guards.
#
#   HighlightExporter.*     — its public data model directly names Annotation and
#                             Bookmark types that are absent when their features are off.
#
# ─────────────────────────────────────────────────────────────────────────────
# CATEGORY 2 — RATCHETED cleanup debt
#
# The repository still contains app-shell ENABLE_* guards that can be migrated
# to src/features/<feature>/Registration.cpp but have not been yet. Compare the
# exact normalized directives with a Git base revision: existing debt is
# grandfathered, removals are welcome, and additions fail this check.
# ─────────────────────────────────────────────────────────────────────────────
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BASE_REF="${1:-HEAD}"
PATTERN='^[[:space:]]*#[[:space:]]*if(def)?[[:space:]]+.*ENABLE_'
PERMANENT_PATHS_REGEX='^src/(SettingsList\.h|CrossPointSettings\.cpp|network/server/CrossPointWebServer\.cpp|network/ota/OtaWebCheck\.cpp|network/wifi/BleWifiProvisioner\.(cpp|h)|util/(UserFontManager|HighlightExporter)\.(cpp|h)|activities/browser/OpdsBookBrowserActivity\.cpp|activities/boot_sleep/SleepActivity\.cpp|activities/reader/(EpubReaderActivity|EpubReaderMenuActivity)\.(cpp|h)):'

if ! git cat-file -e "${BASE_REF}^{commit}" 2>/dev/null; then
    echo "Feature boundary base is not a commit: $BASE_REF" >&2
    exit 2
fi

collect_debt() {
    local ref="${1:-}"
    local matches
    if [[ -n "$ref" ]]; then
        matches=$(git grep -n -E "$PATTERN" "$ref" -- src \
            ':(exclude)src/core/**' ':(exclude)src/features/**' || true)
    else
        matches=$(git grep -n -E "$PATTERN" -- src \
            ':(exclude)src/core/**' ':(exclude)src/features/**' || true)
    fi

    printf '%s\n' "$matches" \
        | sed -E 's/^[^:]+:(src\/)/\1/' \
        | grep -vE "^[^:]+:[0-9]+:[[:space:]]*//" \
        | grep -vE "^[^:]+:[0-9]+:[[:space:]]*#(ifn?def|if)[[:space:]]+ENABLE_SERIAL_LOG([[:space:]]|$)" \
        | grep -Ev "$PERMANENT_PATHS_REGEX" \
        | sed -E 's/^([^:]+):[0-9]+:/\1:/' \
        | LC_ALL=C sort || true
}

BASE_DEBT=$(collect_debt "$BASE_REF")
CURRENT_DEBT=$(collect_debt)
NEW_DEBT=$(comm -13 <(printf '%s\n' "$BASE_DEBT") <(printf '%s\n' "$CURRENT_DEBT"))
if [[ -n "$NEW_DEBT" ]]; then
    echo "Feature boundary violations (new ENABLE_* app-shell guards):"
    echo "$NEW_DEBT"
    echo "Move them to src/features/<name>/Registration.cpp."
    exit 1
fi

current_count=$(printf '%s\n' "$CURRENT_DEBT" | sed '/^$/d' | wc -l)
base_count=$(printf '%s\n' "$BASE_DEBT" | sed '/^$/d' | wc -l)
echo "Feature boundary check passed: no new debt ($current_count current, $base_count at $BASE_REF)."
exit 0
