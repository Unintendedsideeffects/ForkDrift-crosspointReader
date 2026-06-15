# Plan 002: Host-test coverage for UrlUtils and StringUtils pure parsers

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report — do not improvise. When done, update the status row for this plan
> in `plans/README.md`.
>
> **Drift check (run first)**: `git diff --stat 47c7c0c4..HEAD -- src/util/UrlUtils.cpp src/util/UrlUtils.h src/util/StringUtils.cpp src/util/StringUtils.h test/run_host_tests.sh`
> If any of these changed since this plan was written, compare the "Current
> state" excerpts against the live code before proceeding; on a mismatch, treat
> it as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: tests
- **Planned at**: commit `47c7c0c4`, 2026-06-15

## Why this matters

`UrlUtils` and `StringUtils` are small, pure, dependency-free functions that feed
security- and correctness-sensitive paths: `isPrivateLanHttpUrl()` is an SSRF
guard (it must reject hostnames so DNS can't smuggle a public origin past it),
`buildUrl()` assembles OPDS/Terminus request URLs, and `sanitizeFilename()`
truncates untrusted UTF-8 filenames without splitting a multibyte codepoint.
None of them are currently compiled into the host-test binary, so there is **no**
automated signal if a refactor breaks IP-range parsing, URL joining, or UTF-8
boundary handling. `UrlUtils.cpp` was edited the day this plan was written —
actively-evolving, untested logic. These are the cheapest possible coverage wins:
pure functions, no hardware mocks, already runnable under the existing
ASAN+UBSAN host harness.

## Current state

- `src/util/UrlUtils.cpp` / `src/util/UrlUtils.h` — namespace `UrlUtils`. Includes
  only `<algorithm>`, `<cstdlib>`. Public API (from `UrlUtils.h`):
  - `bool isHttpsUrl(const std::string& url)` — true iff starts with `https://`.
  - `bool isPrivateLanHttpUrl(const std::string& url)` — true only for `http://`
    URLs whose host is a **numeric** IPv4 in `10/8`, `172.16/12`, `192.168/16`, or
    `127/8`. Hostnames are rejected by design.
  - `std::string ensureProtocol(const std::string& url)` — prepends `http://` if no `://`.
  - `std::string extractHost(const std::string& url)` — host-with-protocol prefix.
  - `std::string buildUrl(const std::string& serverUrl, const std::string& path)` —
    if `path` has a `://` it's returned as-is; if it starts with `/` it's joined to
    the host root; otherwise it's appended relative to the base with the query
    string stripped.
- `src/util/StringUtils.cpp` / `src/util/StringUtils.h` — namespace `StringUtils`.
  Includes `<Utf8.h>` (from `lib/Utf8/`). Public API:
  - `std::string sanitizeFilename(const std::string& name, size_t maxBytes = 100)` —
    skips leading spaces/dots, replaces `/ \ : * ? " < > |` and control chars with
    `_`, copies whole UTF-8 codepoints up to `maxBytes`, trims trailing spaces/dots,
    returns `"book"` if the result is empty.
- `lib/Utf8/` contains BOTH `Utf8.h` and `Utf8.cpp` — `StringUtils.cpp` needs
  `Utf8.cpp` linked and `-I lib/Utf8` on the include path.
- **Test harness**: `test/run_host_tests.sh` compiles `test/host/*.cpp` (globbed —
  new test files are picked up automatically) plus an **explicit list** of source
  `.cpp` files (the long g++ argument list, lines ~77-120). Source-under-test must
  be added to that explicit list by hand. The doctest framework's `main` lives in
  `test/host/test_main.cpp`; individual test files only `#include "doctest/doctest.h"`
  and declare `TEST_CASE`s.
- **Exemplar test file** to copy the structure from: `test/host/test_firmware_artifact_name.cpp`
  — it `#include "doctest/doctest.h"`, `#include "src/util/FirmwareArtifactName.h"`,
  then has `TEST_CASE("...") { CHECK(...); CHECK_FALSE(...); }`. Follow exactly that shape.

Excerpt of the include block in `test/run_host_tests.sh` you will extend
(the trailing source list, abridged):
```sh
  "$ROOT_DIR/test/host/"*.cpp \
  "$ROOT_DIR/lib/OpdsParser/OpenSearchParser.cpp" \
  ...
  "$ROOT_DIR/src/util/PathUtils.cpp" \
  ...
  "$ROOT_DIR/lib/GfxRenderer/Bitmap.cpp" \
  "$ROOT_DIR/lib/GfxRenderer/BitmapHelpers.cpp" \
  "$BUILD_DIR/md4c.o" \
  "$BUILD_DIR/entity.o" \
  -lexpat \
  -o "$BUILD_DIR/HostTests"
```
The `-I` flags block is just above it (lines ~58-75) and includes
`-I"$ROOT_DIR/src"` and `-I"$ROOT_DIR/lib/..."` entries.

## Commands you will need

| Purpose | Command | Expected on success |
|---------|---------|---------------------|
| Drift check | `git diff --stat 47c7c0c4..HEAD -- src/util/UrlUtils.cpp src/util/StringUtils.cpp test/run_host_tests.sh` | empty (or reconcile) |
| Run host tests | `bash test/run_host_tests.sh` | compiles clean, all doctest cases pass, schema `--check` passes, exit 0 |

Run from the **repo root** (`crosspoint-reader/`). Do NOT run a PlatformIO firmware
build — these are pure additions and a `pio` build is unnecessary and conflicts with
any concurrent build.

## Scope

**In scope**:
- `test/host/test_url_utils.cpp` (create)
- `test/host/test_string_utils.cpp` (create)
- `test/run_host_tests.sh` (add `src/util/UrlUtils.cpp`, `src/util/StringUtils.cpp`,
  `lib/Utf8/Utf8.cpp` to the explicit source list, and `-I"$ROOT_DIR/lib/Utf8"` to the
  include flags **only if not already present**)

**Out of scope** (do NOT touch):
- `src/util/UrlUtils.cpp` / `src/util/StringUtils.cpp` production code — you are
  characterizing existing behavior, **not fixing it**. If you believe you found a bug,
  write a `CHECK` that documents the *current* behavior and note the suspected bug in
  your report; do not change the source. (See STOP conditions.)
- `src/util/DateUtils.cpp` — explicitly excluded. It depends on `HalClock` (no host
  mock exists) and would require building a clock mock; that is a separate effort.
- The doctest framework, `test_main.cpp`, mocks.

## Git workflow

- Do NOT commit, push, or open a PR unless the operator explicitly instructs it.

## Steps

### Step 1: Add the source files to the host-test link list

In `test/run_host_tests.sh`, add these three lines to the explicit source list
(anywhere among the other `"$ROOT_DIR/src/util/..."` entries, before the
`"$BUILD_DIR/md4c.o"` line):
```sh
  "$ROOT_DIR/src/util/UrlUtils.cpp" \
  "$ROOT_DIR/src/util/StringUtils.cpp" \
  "$ROOT_DIR/lib/Utf8/Utf8.cpp" \
```
Then check whether the include block already has `-I"$ROOT_DIR/lib/Utf8"`. Run
`grep -n 'lib/Utf8' test/run_host_tests.sh`. If it is absent, add this line to the
`-I` flags block (near the other `-I"$ROOT_DIR/lib/..."` lines):
```sh
  -I"$ROOT_DIR/lib/Utf8" \
```

**Verify**: `grep -n 'UrlUtils.cpp\|StringUtils.cpp\|lib/Utf8' test/run_host_tests.sh` → at least 3 matches.

### Step 2: Write `test/host/test_url_utils.cpp`

Create the file, modeled on `test/host/test_firmware_artifact_name.cpp`. Cover, at
minimum, these cases (assert the **documented current** behavior):

- `isHttpsUrl`: `"https://x"` → true; `"http://x"` → false; `""` → false.
- `isPrivateLanHttpUrl` TRUE: `"http://10.0.0.5"`, `"http://192.168.1.10/path"`,
  `"http://127.0.0.1:8080"`, `"http://172.16.0.1"`, `"http://172.31.255.255"`.
- `isPrivateLanHttpUrl` FALSE: `"http://8.8.8.8"`, `"http://172.15.0.1"` (below range),
  `"http://172.32.0.1"` (above range), `"http://example.com"` (hostname, not numeric),
  `"https://10.0.0.5"` (https, not http), `"http://"` (no host), `"http://1.2.3.4.evil.com"`
  (contains non-digit/non-dot → rejected).
- `ensureProtocol`: `"example.com"` → `"http://example.com"`; `"http://x"` → unchanged;
  `"https://x"` → unchanged.
- `extractHost`: `"http://example.com/path"` → `"http://example.com"`;
  `"example.com/path"` → `"example.com"`; `"http://example.com"` → `"http://example.com"`.
- `buildUrl`:
  - absolute URL passthrough: `buildUrl("http://h", "https://other/x")` → `"https://other/x"`.
  - absolute path: `buildUrl("http://h.com/base", "/feed")` → `"http://h.com/feed"`.
  - relative path: `buildUrl("http://h.com/base", "feed")` → `"http://h.com/base/feed"`.
  - empty path: `buildUrl("h.com", "")` → `"http://h.com"`.
  - query stripped on relative join: `buildUrl("http://h.com/base?q=1", "feed")` →
    `"http://h.com/base/feed"`.
  - trailing slash base: `buildUrl("http://h.com/base/", "feed")` → `"http://h.com/base/feed"`.

Use `#include "src/util/UrlUtils.h"` and `using namespace UrlUtils;` or fully-qualify.

**Verify**: file exists; `grep -c "TEST_CASE" test/host/test_url_utils.cpp` ≥ 3.

### Step 3: Write `test/host/test_string_utils.cpp`

Create the file. Cover `StringUtils::sanitizeFilename`:
- plain ASCII unchanged: `sanitizeFilename("book.epub", 100)` → `"book.epub"`.
- illegal chars replaced with `_`: `sanitizeFilename("a/b:c*d?e", 100)` → `"a_b_c_d_e"`.
- leading spaces/dots stripped: `sanitizeFilename("  ..name", 100)` → `"name"`.
- trailing spaces/dots trimmed: `sanitizeFilename("name.. ", 100)` → `"name"`.
- empty / all-stripped input falls back to `"book"`: `sanitizeFilename("", 100)` →
  `"book"`; `sanitizeFilename("...", 100)` → `"book"`.
- byte-limit truncation does not split a multibyte codepoint: build a string of
  several 2-byte UTF-8 characters (e.g. repeat `"é"` which is `0xC3 0xA9`), call with a
  `maxBytes` that lands mid-codepoint (e.g. 3), and assert the result length is the
  largest whole-codepoint prefix ≤ maxBytes (i.e. it does **not** end on a half
  codepoint). Assert `result.size()` is even and `result.size() <= 3`.

Use `#include "src/util/StringUtils.h"`.

**Verify**: file exists; `grep -c "TEST_CASE" test/host/test_string_utils.cpp` ≥ 1.

### Step 4: Build and run the full host suite

**Verify**: `bash test/run_host_tests.sh` → compiles clean (no errors/warnings from
your new files), all test cases pass including the new ones, the `--check` schema
step passes, exit 0. The runner prints the doctest summary (`test cases:` / `assertions:`)
— your new cases must appear in the totals.

## Test plan

The deliverable *is* the tests. After Step 4, confirm the doctest summary line shows
an increased test-case count and zero failures. ASAN/UBSAN are enabled by the runner,
so any out-of-bounds read in `sanitizeFilename`'s UTF-8 walk would surface as a
sanitizer abort — treat such an abort as a real finding to report (STOP), not
something to silence.

## Done criteria

ALL must hold:

- [ ] `test/host/test_url_utils.cpp` and `test/host/test_string_utils.cpp` exist
- [ ] `grep -n 'UrlUtils.cpp\|StringUtils.cpp\|lib/Utf8/Utf8.cpp' test/run_host_tests.sh` shows all three added
- [ ] `bash test/run_host_tests.sh` exits 0 with all cases passing (new cases included in the totals)
- [ ] No files outside the in-scope list are modified (`git status`)
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:

- `src/util/StringUtils.cpp` fails to compile/link host-side because `lib/Utf8/Utf8.cpp`
  has its own unmet dependencies (e.g. it includes an Arduino-only header). Report the
  exact error — do not start stubbing libraries.
- A new test reveals that current behavior looks like a genuine bug (e.g. an SSRF
  bypass in `isPrivateLanHttpUrl`, or a UTF-8 over-read). Write the test to capture the
  **current** behavior, mark it clearly in a comment (`// NOTE: suspected bug — current behavior, see report`),
  and STOP to report it rather than changing production code (this is a test-only plan).
- The g++ link fails with undefined symbols pulled in transitively by `UrlUtils.cpp`
  or `StringUtils.cpp` that you cannot resolve by adding their own `.cpp` to the list.

## Maintenance notes

- When new pure utilities are added under `src/util/`, add them to the host-test link
  list and a `test/host/test_*.cpp` in the same pass — the harness globs test files but
  not source.
- Deferred follow-up: `DateUtils.cpp` (`parseIsoDate`, `offsetDate`, `formatDayTitle`,
  etc.) is equally valuable to test but needs a host `HalClock` mock because the file
  pulls in `<HalClock.h>` and the `halClock` global. That mock is a separate, larger
  task — do not attempt it here.
- A reviewer should sanity-check that the new tests assert *intended* behavior, not
  just whatever the code currently returns for under-specified inputs (e.g. malformed
  URLs) — where behavior is genuinely "don't care", keep the assertion loose or omit it.
