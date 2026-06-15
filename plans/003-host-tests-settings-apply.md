# Plan 003: Host-test coverage for the web settings-apply path (`applySettingsJson`)

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md`.
>
> **Drift check (run first)**: `git diff --stat 47c7c0c4..HEAD -- src/network/SettingsApi.cpp src/network/SettingsApi.h src/SettingsList.h test/run_host_tests.sh`
> If any changed since this plan was written, compare the "Current state"
> excerpts against the live code before proceeding; on a mismatch, treat it as a
> STOP condition.

## Status

- **Priority**: P2
- **Effort**: M
- **Risk**: LOW (test-only) / linkage-risk MED — see STOP conditions
- **Depends on**: none (but read plan 002 first — it establishes the host-test-add pattern)
- **Category**: tests
- **Planned at**: commit `47c7c0c4`, 2026-06-15

## Why this matters

`POST /api/settings` is the web control plane for the device: the configurator and
web UI push setting changes through `network::applySettingsJson(const String& body)`.
That function deserializes JSON, walks the full settings list, validates each value
by type (TOGGLE/ENUM/VALUE/STRING), persists valid ones, and returns an applied-count.
There is **zero** automated coverage of it: no test references `applySettingsJson`,
and `src/network/SettingsApi.cpp` is not compiled into the host-test binary. The
behaviors that matter for forward/backward compatibility and robustness — JSON with
missing keys (old client), unknown extra keys (new client), out-of-range ENUM/VALUE
values, malformed JSON returning HTTP 400 — are all unverified. A regression here
silently corrupts user settings or rejects valid writes.

## Current state

- `src/network/SettingsApi.h` declares:
  ```cpp
  namespace network {
  struct SettingsApplyResult {
    int statusCode;
    String contentType;
    String body;
    int appliedCount;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
  };
  String buildSettingsListJson();
  SettingsApplyResult applySettingsJson(const String& body);
  }  // namespace network
  ```
- `src/network/SettingsApi.cpp` — `applySettingsJson`:
  - `deserializeJson` failure → returns `{400, "text/plain", "Invalid JSON: ...", 0}`.
  - Iterates `getSettingsList()`; for each `SettingInfo s` it skips keys absent from
    the JSON (`!doc[s.key].is<JsonVariant>()`), and otherwise validates+persists by
    `s.type`:
    - `TOGGLE`: any truthy int → 1 else 0, always applied.
    - `ENUM`: `val` must be `0..UINT8_MAX` AND a valid option index/persisted value;
      invalid values are silently skipped (not applied).
    - `VALUE`: must be within `s.valueRange.min..max`; else skipped.
    - `STRING`: applied via `s.stringSetter` or copied into the settings struct.
  - Returns `{200, "application/json", <body>, applied}` on success (confirm the exact
    success body by reading the function tail).
  - Includes (top of file): `<ArduinoJson.h>`, `<Logging.h>`, `"CrossPointSettings.h"`,
    `"SdCardFontSystem.h"`, `"SettingsList.h"`, `"SpiBusMutex.h"`,
    `"core/features/FeatureModules.h"`.
- `getSettingsList()` is defined in `src/SettingsList.h` (a header, ~948 lines). It
  builds the `std::vector<SettingInfo>` of all settings with their getters/setters
  bound to `SETTINGS` (the `CrossPointSettings` singleton).
- **Host-test harness** (see plan 002 for the full description): `test/run_host_tests.sh`
  globs `test/host/*.cpp` and links an explicit source list. `CrossPointSettings.cpp`
  is **already** in that list (so `SETTINGS` resolves). `SpiBusMutex.h` is mocked in
  `test/mock/SpiBusMutex.h`. `FeatureCatalog.cpp` and a `FeatureModuleHooks.cpp` mock
  are already linked. ArduinoJson is on the include path. `String` is mocked
  (`test/mock/String.h` / `WString.h`).
- **Exemplar**: `test/host/test_settings.cpp` (existing settings test) and
  `test/host/test_settings_snapshot_api.cpp` (existing test that links a `network`
  API source) — read both to see how settings state is set up host-side and how a
  `network::` API is exercised.

## Commands you will need

| Purpose | Command | Expected on success |
|---------|---------|---------------------|
| Drift check | `git diff --stat 47c7c0c4..HEAD -- src/network/SettingsApi.cpp src/SettingsList.h test/run_host_tests.sh` | empty (or reconcile) |
| Run host tests | `bash test/run_host_tests.sh` | compiles clean, all cases pass, exit 0 |
| Inspect deps of a header before linking | `grep -n '#include' src/SettingsList.h` and `grep -n '#include' src/SdCardFontSystem.h src/core/features/FeatureModules.h` | use to anticipate link needs |

Run from repo root. Do NOT run a PlatformIO firmware build.

## Scope

**In scope**:
- `test/host/test_settings_api.cpp` (create)
- `test/run_host_tests.sh` (add `src/network/SettingsApi.cpp` and **only** the minimal
  additional source files its symbols require — see Step 1)

**Out of scope** (do NOT touch):
- `src/network/SettingsApi.cpp`, `src/SettingsList.h`, `CrossPointSettings.*` —
  characterize, don't modify.
- Adding large new subsystem sources or new mocks to make linking work. If
  `SettingsApi.cpp` needs more than ~2 small additional source files to link, that is a
  STOP condition (the dependency cone is too large for a test-only plan; report it).

## Git workflow

- Do NOT commit, push, or open a PR unless explicitly instructed.

## Steps

### Step 1: Add `SettingsApi.cpp` to the link list and resolve its direct deps

Add to the explicit source list in `test/run_host_tests.sh` (before `"$BUILD_DIR/md4c.o"`):
```sh
  "$ROOT_DIR/src/network/SettingsApi.cpp" \
```
Then run `bash test/run_host_tests.sh` and read the linker output. Resolve **undefined
symbol** errors one at a time by adding the single `.cpp` that defines each missing
symbol (use `grep -rln "<SymbolName>" src/ lib/` to locate it). Expect that
`CrossPointSettings`, `FeatureCatalog`, and the feature-module hooks are already
satisfied. `getSettingsList()` is header-defined in `SettingsList.h`, so it compiles
into `SettingsApi.cpp`'s translation unit directly — no extra source needed for it.

**Budget**: if resolving undefined symbols requires adding more than ~2 small source
files, OR pulls in a font-system / activity / rendering source, **STOP** and report the
dependency chain (see STOP conditions). Do not chase a deep cone.

**Verify**: `bash test/run_host_tests.sh` links and runs (even before you add new test
cases) — the existing suite still passes with `SettingsApi.cpp` linked in.

### Step 2: Write `test/host/test_settings_api.cpp`

Model on `test/host/test_settings_snapshot_api.cpp`. Use the `network::` API. Cover:

- **Malformed JSON → 400**: `applySettingsJson("{not json")` → `result.statusCode == 400`,
  `result.ok() == false`, `result.appliedCount == 0`.
- **Empty object → 200, 0 applied**: `applySettingsJson("{}")` → `statusCode == 200`,
  `appliedCount == 0`.
- **Unknown keys ignored**: `applySettingsJson("{\"totallyUnknownKey\":5}")` → `200`,
  `appliedCount == 0` (no setting matches, nothing applied).
- **A valid TOGGLE applies and is readable**: pick a real toggle key by reading
  `src/SettingsList.h` (search for `SettingType::TOGGLE` and note its `.key` string and
  the `SETTINGS` field it binds). Set it to a known state first, POST the opposite via
  `applySettingsJson("{\"<key>\":1}")`, assert `appliedCount >= 1` and the corresponding
  `SETTINGS.<field>` now reflects the new value.
- **An out-of-range ENUM/VALUE is rejected**: pick a `SettingType::VALUE` key with a
  known `valueRange`, POST a value above `max`, assert it is **not** counted in
  `appliedCount` and `SETTINGS.<field>` is unchanged.
- **Type-mismatch tolerance**: POST a string where an int is expected for a real key
  (e.g. `"{\"<toggleKey>\":\"oops\"}"`). Assert the function does not crash and returns
  `200` (document whatever the current applied-count is — ArduinoJson's `as<int>()`
  coerces; capture the real behavior).

Read the actual key strings and field names from `src/SettingsList.h` — do **not**
invent key names. If you cannot confidently identify a stable TOGGLE and VALUE key,
STOP and report (better than a brittle test on a guessed key).

**Verify**: `grep -c "TEST_CASE" test/host/test_settings_api.cpp` ≥ 3.

### Step 3: Build and run the full suite

**Verify**: `bash test/run_host_tests.sh` → compiles clean, all cases pass (new ones
included in the totals), schema `--check` passes, exit 0.

## Test plan

- New file `test/host/test_settings_api.cpp` with the cases in Step 2.
- Structural pattern: `test/host/test_settings_snapshot_api.cpp`.
- Verification: `bash test/run_host_tests.sh` → all pass, doctest totals increased.

## Done criteria

ALL must hold:

- [ ] `test/host/test_settings_api.cpp` exists with ≥ 3 `TEST_CASE`s
- [ ] `src/network/SettingsApi.cpp` is in the `test/run_host_tests.sh` source list
- [ ] `bash test/run_host_tests.sh` exits 0 with all cases passing
- [ ] At most ~2 additional small source files were added to the link list (else STOP was hit)
- [ ] No files outside the in-scope list are modified (`git status`)
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- Linking `SettingsApi.cpp` requires adding more than ~2 small source files, or drags in
  a font-system / rendering / activity source. Report the full chain of undefined symbols
  and the sources that would be needed — the maintainer may prefer extracting a
  pure-logic core, which is out of scope here.
- You cannot identify stable, real setting keys (TOGGLE and VALUE) from `src/SettingsList.h`
  to write non-brittle assertions against.
- A test reveals an apparent real bug in `applySettingsJson` (e.g. an out-of-range value
  being applied). Capture current behavior in the test with a clear `// NOTE` comment and
  STOP to report — do not change production code.
- The success-path response body/shape differs from what's documented here in a way that
  makes the asserts ambiguous.

## Maintenance notes

- When a new `SettingInfo` row is added with a new validation rule, add a case here.
- If the maintainer later extracts the per-type validation into a pure helper, this test
  should target that helper directly (smaller dependency cone).
- A reviewer should confirm the chosen TOGGLE/VALUE keys are not ones likely to be renamed
  soon, and that the type-mismatch case documents *current* coercion behavior intentionally.
