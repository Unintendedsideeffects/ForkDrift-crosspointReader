# Plan 004: Extract pure firmware-update helpers and host-test them

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md`.
>
> **Drift check (run first)**: `git diff --stat 47c7c0c4..HEAD -- src/util/FirmwareUpdateUtil.cpp src/util/FirmwareUpdateUtil.h test/run_host_tests.sh`
> If any changed since this plan was written, compare the "Current state"
> excerpts against the live code before proceeding; on a mismatch, treat it as a
> STOP condition.

## Status

- **Priority**: P2
- **Effort**: M
- **Risk**: MED (touches production code — a small refactor, not a pure test-add)
- **Depends on**: none (read plan 002 first for the host-test-add pattern)
- **Category**: tests (+ minimal refactor to enable them)
- **Planned at**: commit `47c7c0c4`, 2026-06-15

## Why this matters

The local firmware-update path is a bricking-class feature: it fingerprints a
firmware `.bin` on the SD card, decides whether it differs from what the user
already skipped, and extracts a human-readable version string out of the binary to
show in the prompt. The pure logic behind that — an FNV-1a hash update, a
"is this byte a printable version char" predicate, and a marker-matching state
machine used to locate the version string — currently lives in an **anonymous
namespace inside `src/util/FirmwareUpdateUtil.cpp`**, interleaved with `esp_ota_*`,
`HalStorage`, and `HalFile` calls. Because it's anonymous and entangled with
hardware APIs, none of it is host-testable, and there is no coverage of the bit
that's easy to get subtly wrong: scanning a byte stream for a version marker and
collecting the trailing printable run.

This plan does the **minimum** refactor to make the pure helpers testable — lifting
three already-pure free functions into a small new compilation unit — then tests
them. It deliberately does **not** try to host-test the HAL/ESP-coupled functions
(`performLocalUpdate`, `readFirmwareAppDescription`, file I/O), which would require
heavy stubbing and are out of scope.

## Current state

- `src/util/FirmwareUpdateUtil.h` — declares only the public `FirmwareUpdateUtil`
  class with static methods (`checkForLocalUpdate`, `handleLocalUpdateBootFlow`,
  `performLocalUpdate`) that take `GfxRenderer&`/`MappedInputManager&` and drive the
  hardware flow. The pure helpers are NOT declared here.
- `src/util/FirmwareUpdateUtil.cpp` — includes `<HalStorage.h>`, `"esp_ota_ops.h"`,
  etc. Inside an `namespace { ... }` (starts ~line 26) it defines, among others, these
  **pure** functions (no HAL/ESP dependency — verify each by reading it):
  - `uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, size_t length)` (~line 47)
    — standard FNV-1a incremental hash over a buffer.
  - `bool isFirmwareVersionChar(char ch)` (~line 55) — `ch >= '!' && ch <= '~' && ch != '"' && ch != '\\'`.
  - `void advanceMarkerMatch(char ch, const char* marker, size_t& matchLength)` (~line 57)
    — advances/resets a running match against a literal marker string (the state machine
    used by `readCrossPointVersionMarker` to find `"Starting CrossPoint version "`).
  - (Entangled, NOT pure — leave in place: `hashLocalUpdateSample`, `readFirmwareAppDescription`,
    `readCrossPointVersionMarker`, the `Storage.*` helpers — these take `HalFile&`/use `Storage`.)
- The marker used by the scan is `constexpr char bootLogMarker[] = "Starting CrossPoint version ";`
  inside `readCrossPointVersionMarker` (~line 203), and version chars are gated by
  `isFirmwareVersionChar` with a max length cap.
- **Host-test harness**: see plan 002. `test/host/*.cpp` is globbed; sources are listed
  explicitly. `lib/Serialization` is already on the include path (the file uses
  `serialization::writePod/readPod`, but those are only in the entangled functions you
  will not move).

## The refactor shape

Create a new pure unit:
- `src/util/FirmwareUpdateHelpers.h` — declares the three pure functions in a namespace
  `firmware_update` (or similar), with doc comments.
- `src/util/FirmwareUpdateHelpers.cpp` — defines them. Includes only `<cstdint>` /
  `<cstddef>` (no HAL, no ESP headers).

Then in `src/util/FirmwareUpdateUtil.cpp`: remove the three functions from the anonymous
namespace, `#include "util/FirmwareUpdateHelpers.h"`, and update the call sites to use
the namespaced names. **Behavior must be identical** — this is a pure move, not a rewrite.

Add one host-testable seam for the marker scan: also add to `FirmwareUpdateHelpers`
a pure, buffer-based version of the version-extraction logic so the scan itself can be
tested without `HalFile`:
- `std::string extractVersionAfterMarker(const uint8_t* data, size_t length, const char* marker)`
  — runs `advanceMarkerMatch` across `data`; once the marker fully matches, collects the
  following run of `isFirmwareVersionChar` bytes (stopping at the first non-version char,
  capped at the same max length the production code uses), trims, and returns it (empty if
  not found). Then refactor `readCrossPointVersionMarker` to read the firmware bytes into a
  buffer (it already reads in chunks) and delegate the *decision logic* to
  `extractVersionAfterMarker` where practical. **If that delegation can't be done cleanly
  without changing behavior, leave `readCrossPointVersionMarker` as-is and just add+test
  `extractVersionAfterMarker` as the canonical pure implementation** — note the duplication
  in your report rather than forcing a risky change.

## Commands you will need

| Purpose | Command | Expected on success |
|---------|---------|---------------------|
| Drift check | `git diff --stat 47c7c0c4..HEAD -- src/util/FirmwareUpdateUtil.cpp test/run_host_tests.sh` | empty (or reconcile) |
| Run host tests | `bash test/run_host_tests.sh` | compiles clean, all cases pass, exit 0 |
| Verify firmware still builds | `uv run pio run -e default` | `[SUCCESS]` in output, exit 0 — **only if no other build is running** |

For the firmware build: do NOT pipe through `tail` (it masks pio's exit code); grep the
output for `[SUCCESS]` / `error:`. Never run it concurrently with another pio build.

## Scope

**In scope**:
- `src/util/FirmwareUpdateHelpers.h` (create)
- `src/util/FirmwareUpdateHelpers.cpp` (create)
- `src/util/FirmwareUpdateUtil.cpp` (move the 3 pure functions out; update call sites; minimal)
- `test/host/test_firmware_update_helpers.cpp` (create)
- `test/run_host_tests.sh` (add `src/util/FirmwareUpdateHelpers.cpp`)

**Out of scope** (do NOT touch):
- The HAL/ESP-coupled functions and the public `FirmwareUpdateUtil` class API — no
  signature changes to `checkForLocalUpdate`/`performLocalUpdate`/`handleLocalUpdateBootFlow`.
- `src/network/OtaUpdater.cpp` — the network OTA path; out of scope for this plan.
- Any change to the FNV-1a algorithm or the marker string — a pure move only; the
  fingerprint must stay byte-identical or it would invalidate the on-device skip records.

## Git workflow

- Do NOT commit, push, or open a PR unless explicitly instructed. If you must isolate
  work, a branch `refactor/ota-helpers` is fine, but do not commit through the pre-commit
  hook (it runs a firmware build).

## Steps

### Step 1: Create the pure helpers unit (move, don't rewrite)

Create `src/util/FirmwareUpdateHelpers.h` and `.cpp` and move `fnv1aUpdate`,
`isFirmwareVersionChar`, `advanceMarkerMatch` verbatim into namespace `firmware_update`.
Copy the bodies exactly. Add `extractVersionAfterMarker` per "The refactor shape".

**Verify**: `uv run pio run -e default` builds clean **after** Step 2 wires the callers —
do not build between Step 1 and Step 2 (the source won't compile mid-move).

### Step 2: Point `FirmwareUpdateUtil.cpp` at the moved helpers

Remove the three moved functions from the anonymous namespace in
`src/util/FirmwareUpdateUtil.cpp`, add `#include "util/FirmwareUpdateHelpers.h"`, and
qualify the call sites (`firmware_update::fnv1aUpdate(...)`, etc.). Make no behavioral
changes.

**Verify**: `uv run pio run -e default` → `[SUCCESS]`, exit 0 (only if no other build is
running). `grep -n "fnv1aUpdate\|advanceMarkerMatch\|isFirmwareVersionChar" src/util/FirmwareUpdateUtil.cpp`
→ all references now namespaced; no leftover local definitions.

### Step 3: Add the helpers source to the host-test link list

In `test/run_host_tests.sh`, add (before `"$BUILD_DIR/md4c.o"`):
```sh
  "$ROOT_DIR/src/util/FirmwareUpdateHelpers.cpp" \
```

**Verify**: `grep -n FirmwareUpdateHelpers test/run_host_tests.sh` → 1 match.

### Step 4: Write `test/host/test_firmware_update_helpers.cpp`

Model on `test/host/test_firmware_artifact_name.cpp`. Cover:
- `fnv1aUpdate`: known-answer — hashing the ASCII bytes of a fixed string from the
  FNV-1a 32-bit offset basis (`0x811c9dc5`) yields the well-known FNV-1a value; assert
  two different inputs differ and the same input is deterministic. (Compute the expected
  value by running the algorithm in the test itself over a literal and asserting
  stability + that a 1-byte change changes the hash.)
- `isFirmwareVersionChar`: `'1'`,`'.'`,`'A'`,`'~'`,`'!'` → true; `'"'`,`'\\'`,`' '` (space),
  `'\n'`,`'\0'` → false.
- `advanceMarkerMatch`: feed the characters of the marker `"abc"` one at a time and assert
  `matchLength` reaches 3; feed `"abxabc"` and assert it correctly resets and re-matches
  to 3 by the end.
- `extractVersionAfterMarker`: build a byte buffer containing
  `"...junk...Starting CrossPoint version 1.2.3\x00more"` and assert it returns `"1.2.3"`;
  a buffer without the marker returns `""`; a marker at the very end with no trailing
  version chars returns `""`.

**Verify**: `grep -c "TEST_CASE" test/host/test_firmware_update_helpers.cpp` ≥ 3.

### Step 5: Run both gates

**Verify**:
- `bash test/run_host_tests.sh` → all pass (new cases included), exit 0.
- `uv run pio run -e default` → `[SUCCESS]`, exit 0 (no concurrent build).

## Test plan

- New file `test/host/test_firmware_update_helpers.cpp` (cases above).
- Pattern: `test/host/test_firmware_artifact_name.cpp`.
- Verification: host tests pass AND the firmware still builds (the refactor must be
  behavior-preserving and compile on the real target).

## Done criteria

ALL must hold:

- [ ] `src/util/FirmwareUpdateHelpers.{h,cpp}` exist; the 3 pure functions live there, namespaced
- [ ] `src/util/FirmwareUpdateUtil.cpp` references them via the new unit; no duplicate local defs (`grep`)
- [ ] `test/host/test_firmware_update_helpers.cpp` exists with ≥ 3 `TEST_CASE`s
- [ ] `bash test/run_host_tests.sh` exits 0 with all cases passing
- [ ] `uv run pio run -e default` reports `[SUCCESS]` (firmware still compiles)
- [ ] No files outside the in-scope list are modified (`git status`)
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- The three functions named in "Current state" are not actually pure (they reference
  `Storage`, `HalFile`, `esp_*`, or a file-scope variable) — the move would change
  behavior. Report what you found.
- Refactoring `readCrossPointVersionMarker` to delegate to `extractVersionAfterMarker`
  cannot be done without changing observable behavior — in that case add+test the pure
  helper as the canonical implementation and leave the production scanner untouched,
  noting the duplication (this is acceptable; do not force the change).
- The firmware build (`pio run -e default`) fails after the move and you cannot make it
  pass within two attempts, OR a PlatformIO build is already running (never start a second).
- The FNV-1a result or any fingerprint changes as a result of your move — the hash must be
  byte-identical to before.

## Maintenance notes

- The on-device "skipped update" record stores `fnv1aUpdate`-derived fingerprints; never
  alter the hash function or marker string without bumping the persisted-record version in
  `FirmwareUpdateUtil.cpp` (search for `kSkippedLocalUpdateVersion`).
- A reviewer should diff the moved function bodies against the originals character-for-
  character to confirm the move was verbatim, and confirm no esp/HAL include leaked into
  `FirmwareUpdateHelpers.cpp`.
- Deferred: host-testing the HAL/ESP-coupled OTA flow (`readFirmwareAppDescription`, the
  retry/backoff in `OtaUpdater.cpp`, SHA256 verification) needs `esp_ota_*` + HTTP + SD
  stubs — a separate, larger effort.
