# SD language files

CrossInk includes only English in the default firmware (`custom_i18n_builtin_langs = en`).
Custom builds may embed an explicit comma-separated subset, or `all`. English is
always included. Other languages come from editable SD files and are prepared
once in internal flash. Opening menus, reading, and waking do not read translation
files from the SD card.

## Install or edit a language

1. Download the language ZIP for your firmware release and copy its
   `.crosspoint/languages` folder to the root of your SD card. You can keep only
   the files you need. Dot-prefixed folders may be hidden in your file manager.
2. Open **Settings → System → Device → Language** and select the language. CrossInk displays a
   loading screen, prepares the file, saves your choice, and restarts to Home.
3. To apply edits, select that language again. Editing a file alone does not
   change the installed copy. Keep the source file for future edits or reinstalling.

The cached language remains available if its source file is removed and takes
precedence over a built-in translation of the same code. A matching SD file in
the picker reinstalls that language, even if it is also built in. Without a valid
cache, a compiled-in translation is used; otherwise the UI falls back to English.
English is always available. A file that fails validation does not replace the running
language. The device log explains the failed check. Duplicate language codes in
multiple SD files are disabled until you remove or rename the duplicate identity.

When upgrading from firmware with built-in translations, the saved language code
is preserved. An English-only build initially uses English until you install its
SD language file. A custom build with that translation embedded can use it immediately.
Later firmware updates reuse compatible cached translations; missing keys
or keys whose English reference changed use English until you reapply the file.
The version label in a YAML file is informational, not an exact-version lock.

## Create a translation

Copy `english_template.yaml` (`english-template.yaml` remains an archive alias), choose a filename ending in lowercase `.yaml`, and
change its language code and native name. You may remove untranslated entries.
A new language identity does not require a firmware change.

```yaml
_language_code: "MY-LANGUAGE"
_language_name: "My language"
_direction: "ltr"
_keyboard: "EN"

# Keep the STR_ names; edit the quoted text.
STR_SETTINGS_TITLE: "My settings"
STR_TIME_EXAMPLE: "Unknown keys are ignored"
```

- Metadata belongs before the first `STR_` entry. `_language_code` and
  `_language_name` are required. Codes are at most 31 ASCII letters, digits,
  hyphens, or underscores, start with a letter, and are normalized to uppercase.
  `EN` is reserved for the built-in English language. Native names are at most
  95 UTF-8 bytes. Keep names on one line.
- `_direction` is `ltr` or `rtl`. Without it, Arabic (`AR`) and Hebrew (`HE`)
  retain their existing RTL controls; other identities default to LTR.
- `_keyboard` selects an existing language's keyboard default. Unknown codes
  fall back to English. Explicitly enabled keyboard layouts still take precedence.
  Existing legacy codes such as `PT`, `PT2`, and `SI` retain their meanings.
- Use UTF-8 and double-quoted strings. Supported escapes are `\"`, `\\`, `\n`,
  and `\t`. Blank lines, whole-line comments, and comments after a closing quote
  are supported. YAML nesting, lists, multiline blocks, and Unicode escapes are
  not supported; type the Unicode characters directly.
- Missing or empty translations use English. Unknown `STR_` keys are ignored.
  For strings containing format placeholders, preserve their argument types and
  order, including length modifiers such as `%zu`. Unsafe/mismatched formats
  fall back to English for that entry. Positional arguments and `%n` are not
  supported. Do not translate placeholder syntax.
- Limits: 256 KiB source file, 2,047 bytes per source line, 127-byte key names,
  256 unknown/metadata keys, and a 64 KiB compiled cache including its metadata.
  The picker scans at most 64 YAML files, with filenames below 127 bytes.
  Codes, names, and filenames share a 2 KiB catalog budget (all 27 starter files
  fit together). If the catalog is too large, keep fewer files on the SD card;
  English and the active cached language remain available.
- Font glyph coverage and available keyboard layouts are unchanged. A new
  translation file cannot add missing font glyphs, shaping rules, or keyboards.

## Flash storage and recovery

The existing `spiffs` data region is separate from the two firmware application
slots. CrossInk uses its final 128 KiB as two 64 KiB language slots. It does not
mount a filesystem there, resize partitions, or overwrite an OTA/rollback image.

CrossInk owns this firmware data partition. Applying an SD language initializes
its selected cache slot automatically, even if another firmware left files or
filesystem metadata there. Existing SPIFFS files are not preserved as a usable
filesystem; back them up beforehand if you need them. Only the selected 64 KiB
slot is erased and written during each installation, with the other slot left
intact. No manufacturer-specific file or layout detection is needed.

Booting, waking, and selecting English do not initialize flash storage. Removing
SD cache folders does not clear internal flash. Normal OTA and SD firmware
updates leave this region alone; a complete flash erase removes the installed
language. The partition table, both application slots, boot metadata, NVS,
coredump storage, and SD card are outside the language cache write range.

Installation writes the unselected slot and publishes it only after readback and
checksum validation. The current slot stays mapped until restart. Settings use
synced temporary files with backup recovery, so an interrupted selection can
return to the previous language. As with other SD operations, physical card or
filesystem damage can still require recovery from a backup.

## Maintaining firmware and releases

The generator reads `english.yaml`, `languages.json`, and only the YAML files
selected by `custom_i18n_builtin_langs`. `en` is the default; `en,es,fr` embeds
those three languages, and `all` embeds all registered starter languages. Invalid
codes, duplicate selected identities, or missing selected sources fail the build.
Missing translations and unsafe printf contracts fall back to English. Unselected
starter files are not validated or embedded by an English-only build.
`languages.json` freezes legacy enum/code/name compatibility; it is not the SD
language catalog. Adding an English key does not require changes to community
translations. Prefer `_FORMAT` for new printf-format keys; existing exceptions
are listed in `scripts/gen_i18n.py` so their argument contracts are checked.
Do not repurpose an existing `STR_` key for an unrelated meaning.

Release and release-candidate workflows attach a language ZIP. To create one:

```sh
python3 scripts/package_languages.py --version 1.5.2 --output /tmp/crossink-languages.zip
```

Community files remain starter assets and may be incomplete. Missing keys do not
block firmware builds. The release template identifies the corresponding
English source, and the ZIP manifest records checksums.

### Automatic template for translation editors

`scripts/generate_language_template.py` generates `english_template.yaml` and
`language-template.json` from the same `english.yaml` and `gen_i18n.py` rules used
by firmware. No second string catalog is maintained. Existing `STR_` IDs and
English source order are preserved, including IDs currently unused by firmware.
The JSON's `active` flag uses the same source scan as firmware key stripping.
Only active nonempty translations that differ from English consume cache records.
Empty translations mean English fallback; whitespace-only translations are literal
text in the SD parser.

Editors should load the **single JSON document** at
`https://raw.githubusercontent.com/uxjulia/CrossInk/main/docs/languages/template/language-template.json`.
This public generated source supports browser cross-origin fetching. It contains the
English entries, printf argument signatures, parser limits, legacy keyboard
identities, and exact source provenance together, so two requests cannot mix
releases. `schemaVersion: 1` describes this editor contract. `format` names the
firmware parser format. `sourceCommit` identifies the source snapshot, and
`firmwareVersion` is informational rather than a device version lock.
`englishSourceSha256` hashes the exact English source bytes; `keysetSha256` hashes
all ordered entry keys joined by LF **with a trailing LF**. The companion YAML's
exact SHA-256 is recorded in `template.sha256`. All hashes use UTF-8 bytes.

Stable release builds generate the catalog, template and editor starter assets.
They attach the catalog, template and language ZIP to the draft release and copy
the same editor assets beside the existing Pages release catalog. The site asset sync copies `docs/languages/template` to
`/languages/template`, also available through the CrossInk website. The JSON
reports `status: "release-build"`: the workflow
publishes the Pages catalog before a human publishes its draft GitHub Release,
so this status deliberately does not claim that a release has been published.
Release candidates attach their own assets but do not replace the stable Pages
copy.

For each available non-English starter source, a language identity in the JSON
has an optional `translation` descriptor with `filename`, `sha256`, and
`omittedKeys`. Paths have the form `translations/<sha256>.yaml`, relative to the
catalog. Inky loads and checksum-validates the selected language file, checks
its language identity and template provenance, then opens those translations
for local editing. The catalog remains fixed during editing. Template changes
cannot redirect an old catalog to different translation text: filenames are
content-addressed and stable publication adds new files while preserving older
hashes. The ZIP also includes these editor starter files.

Editor starters are generated from the existing community YAML sources. They
contain only current canonical keys and preserve nonempty translations exactly,
including English-identical and whitespace-only values. Obsolete keys, empty
values and unsafe printf contracts are omitted for English fallback.
`omittedKeys` counts entries removed from the **source starter**, including
obsolete IDs; it is not a count of missing canonical translations. Direction
and keyboard defaults retain the source language's existing behavior. Each file
has firmware version, source commit, English-source and keyset hashes alongside
language metadata. All normalized files must pass device UTF-8, scalar, line,
source and compiled-cache limits before generation succeeds.

Public fetching becomes available only after this local plumbing is
integrated and the release/Pages workflows run; an older public catalog may
contain a different keyset from local development firmware.

For an exact local firmware snapshot, generate an offline fixture instead:

```sh
python3 scripts/generate_language_template.py --version local-test-languages \
  --source-commit "$(git rev-parse HEAD)" --output-dir /tmp/crossink-template
```

The default `status: "local-fixture"` distinguishes this output from live release
builds. Inky can load this JSON locally, display its source/version, and export a
translation without sending translation text to any server. Keep the chosen
contract while editing; explicitly load another version before migrating keys.
Metadata must remain ahead of strings. Inky export adds the firmware version,
source commit, English-source hash and keyset hash as informational metadata; unsupported
`STR_` entries should be rejected by the editor even though firmware ignores them.
All device scalar, UTF-8, format, source, line and cache limits still apply.
Translations do not add CJK glyphs, font coverage, keyboards or shaping support.

## Validation and timing

The installer uses less than 5 KiB of parser scratch. Catalog and parser heap
allocations are fallible and together stay below 8 KiB; the catalog is released
before installation. The normal lookup index uses two internal-RAM bytes per key.

The host suite exercises the production parser, cache, generated schema, flash
adapter, and I18n API. It covers restart/apply behavior, all starter languages,
missing SD sources, fallback, and stable string pointers:

```sh
cmake -G 'Unix Makefiles' -S test/language_cache -B /tmp/crossink-language-tests
cmake --build /tmp/crossink-language-tests -j2
ctest --test-dir /tmp/crossink-language-tests --output-on-failure
```

After building the simulator, exercise the real settings migration and picker:

```sh
python3 scripts/test_sd_language_boot.py --installer /tmp/crossink-language-tests/I18nIntegrationTool --ui
```

For hardware timing, build with `CROSSINK_LANGUAGE_BENCHMARK` and package with
`--benchmark`. Install **English benchmark**. Compare the same Settings/reader
menu interactions with built-in English, mapped flash, and the diagnostic PSRAM
backend on S3 (add `CROSSINK_LANGUAGE_BACKEND_PSRAM` for that comparison build).
Keep `CROSSINK_LANGUAGE_BENCHMARK` enabled so the fixture retains identical English
strings in its cache. Diagnostic allocation/timing is excluded from normal firmware.
Record warm and cold runs, internal free heap/largest block, and installation
and cached startup times separately. Rendering logs distinguish CPU preparation
from time spent in display calls. Target added p95 CPU rendering below 5 ms and
cached wake overhead below 20 ms. Host timings cannot establish these targets
on an ESP32 or a physical e-ink panel.

Test X3/X4, Sticky, and X4 Pro: change/reapply a language, remove its SD source,
wake repeatedly, exercise RTL controls, then perform OTA and SD firmware updates.
Check that the cached selection survives and both firmware-slot behaviors remain
intact. Interrupt installation only on a backed-up test device. No EPUB cache
reset is needed for this feature.
