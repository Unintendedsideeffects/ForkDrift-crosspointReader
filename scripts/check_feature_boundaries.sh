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
# ─────────────────────────────────────────────────────────────────────────────
# CATEGORY 2 — TEMPORARY cleanup debt
#
# These files contain ENABLE_* guards that CAN be migrated to
# src/features/<feature>/Registration.cpp but have not been yet.
# Each entry below is a TODO: remove it from CLEANUP_DEBT_EXCLUDES (and from
# this script) once the migration PR for that file lands.
# The script emits a non-blocking warning for any violations still present.
# ─────────────────────────────────────────────────────────────────────────────
#
#   BackgroundWebServer.cpp — TODO: move whole-subsystem guard to Registration.cpp
#                             (ENABLE_BACKGROUND_SERVER guard wrapping server task;
#                             the class can be compiled independently of the guard).
#
#   TxtReaderActivity.cpp   — TODO: split markdown helper functions
#                             (isHorizontalRule etc.) into a separate compilation
#                             unit so the guard lives in Registration.cpp instead.
#                             (ENABLE_MARKDOWN guards at helper-function definitions)

set -e

PERMANENT_PATHS_REGEX='^src/(CrossPointSettings\.cpp|network/CrossPointWebServer\.cpp|network/OtaWebCheck\.cpp|network/BleWifiProvisioner\.(cpp|h)|util/UserFontManager\.(cpp|h)|activities/browser/OpdsBookBrowserActivity\.cpp|activities/boot_sleep/SleepActivity\.cpp):'

# TODO: Remove each entry here once its migration PR lands (Category 2 above).
CLEANUP_DEBT_PATHS_REGEX='^src/(network/BackgroundWebServer\.cpp|activities/reader/TxtReaderActivity\.cpp):'

ALL_MATCHES=$(
    grep -rEn "^[[:space:]]*#[[:space:]]*if(def)?[[:space:]]+.*ENABLE_" src/ \
    --exclude-dir=core \
    --exclude-dir=features \
    | grep -vE "^[^:]+:[0-9]+:[[:space:]]*//" \
    | grep -vE "^[^:]+:[0-9]+:[[:space:]]*#(ifn?def|if)[[:space:]]+ENABLE_SERIAL_LOG([[:space:]]|$)" || true
)

# Hard-fail on violations outside both exemption categories.
violations=$(
    echo "$ALL_MATCHES" \
    | grep -Ev "$PERMANENT_PATHS_REGEX|$CLEANUP_DEBT_PATHS_REGEX" || true
)

if [ -n "$violations" ]; then
    echo "Feature boundary violations (move to src/features/<name>/Registration.cpp):"
    echo "$violations"
    exit 1
fi

# Non-blocking warning for remaining cleanup-debt violations.
# Each warning here corresponds to a TODO in CLEANUP_DEBT_EXCLUDES above.
debt_violations=$(
    echo "$ALL_MATCHES" | grep -E "$CLEANUP_DEBT_PATHS_REGEX" || true
)

if [ -n "$debt_violations" ]; then
    echo "⚠ Cleanup-debt ENABLE_* violations still present (non-blocking — remove"
    echo "  exclusions from CLEANUP_DEBT_EXCLUDES when each migration PR lands):"
    echo "$debt_violations"
fi

exit 0
