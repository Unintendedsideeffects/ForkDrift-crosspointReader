# Findings register

Shared, append-only record of problems found **outside the scope of the work that found
them**. Common to every agent and every human on this project.

An out-of-scope discovery is the most perishable output of any task: the person editing
one file is the only one who will ever see the dead code beside it. If that observation
lives only in a log tail or a chat transcript, it is gone. This file is where it goes
instead.

## Rules

- **Do not fix out-of-scope problems in passing.** Silently widening a change destroys
  the reviewability of the diff, which is the entire point of having a scope.
- **Report it** in your summary, then **append an entry here**.
- **Append only**, newest at the bottom. Never rewrite someone else's entry — change its
  **Status** line instead.
- Entries are **self-contained**: the reader has no context from your session.

Delegated agents must be given this file's absolute path in their brief.

### Entry format

```markdown
## <ISO-8601 UTC timestamp> — <one-line claim>
- **Found by**: agy | codex | claude | human — during plan <NNN> (or: ad hoc)
- **Where**: `path/to/file.cpp:123`
- **What**: what is actually wrong, in one or two sentences.
- **Why not fixed here**: out of scope for <work> (scope was <files>).
- **Status**: open | planned as <NNN> | fixed in <commit> | wontfix (<reason>)
```

## Regression escalation ladder

How hard we defend against a regression is set by **how many times its class has
occurred**, not by how severe it felt:

| Occurrence | Required response |
|---|---|
| **1st** | **A test.** Reproduce it in the suite. A fix without a test is not a fix. |
| **2nd** | **Documentation.** The test did not change behaviour, so record *why* this keeps happening — a comment at the trap, and a note in the docs. |
| **3rd** | **A gate.** A CI leg, a pre-commit check, a required build-matrix entry. Something that fails automatically. |

The ladder only ratchets up. Rung 3 means rungs 1 and 2 failed, so the gate is added *in
addition to* the test and the docs, never instead of them.

Counts apply to **classes**, not to literally identical bugs. Two different compile
breaks both rooted in "no build exercises this compile gate" are the same regression
twice.

---

# Findings

## 2026-07-26T11:20Z — `MenuAction::TOGGLE_BOOKMARK` is a dead menu entry in every build

- **Found by**: claude — during plan 086
- **Where**: `src/activities/reader/EpubReaderMenuActivity.cpp:63`, enum at
  `src/activities/reader/EpubReaderMenuActivity.h:30`
- **What**: The reader menu pushes a `TOGGLE_BOOKMARK` row, but no `switch` anywhere
  handles `MenuAction::TOGGLE_BOOKMARK` — selecting it does nothing. It is easy to
  mistake for handled because `ShortcutAction::TOGGLE_BOOKMARK`
  (`src/CrossPointSettings.h:184`) is a **different enum with the same name**, and its
  handlers at `EpubReaderActivity.cpp:1065,1112,1157` grep as if they were the menu's.
  The functioning menu action is the separate, gated `MenuAction::BOOKMARK_TOGGLE`
  (`.h:42`), which pushes "Add/Remove Bookmark". So the menu likely shows a dead
  "Toggle Bookmark" row directly above a working one.
- **Why not fixed here**: out of scope for plan 086, whose scope was making the
  `ENABLE_BOOKMARKS=0` state compile with no ON-state behaviour change. Deleting a
  visible menu row is a behaviour change.
- **Status**: open — needs on-device confirmation that the row appears and is inert
  before deciding between deleting the row or wiring it up.

## 2026-07-26T11:20Z — no build in CI or local rotation exercises `ENABLE_BOOKMARKS=0`

- **Found by**: claude — during the 065–074 gate battery
- **Where**: `.github/workflows/feature-matrix-test.yml`, `platformio.ini`
- **What**: `default`, `custom`, `simulator` and all release environments leave
  `ENABLE_BOOKMARKS` on. The OFF state — a **supported configurator profile** — was
  uncompilable and nobody could have noticed. This is the second compile break found in
  one session in code no routine build compiles (the first: a half-applied rename in
  `BackgroundWifiService.cpp`, invisible because the host suite never compiles that file
  and the branch was never pushed).
- **Why not fixed here**: widening CI is a cost-bearing decision for the maintainer, not
  something a scoped fix should do unilaterally.
- **Status**: open — **this is the class's 2nd occurrence, so per the ladder it earns
  documentation now** (this entry) **and a gate on the third.** The natural gate is a
  `feature-matrix-test.yml` leg building at least one bookmarks-off configuration.

## 2026-07-26T11:20Z — settings-persistence tests cannot fail for the production code they cover

- **Found by**: claude — reviewing plan 069's out-of-scope edit to a shared host mock
- **Where**: `test/run_host_tests.sh:147` links `test/mock/JsonSettingsIO.cpp`;
  `src/JsonSettingsIO.cpp` is **never** linked into the host suite. Tests at
  `test/host/test_settings_persistence.cpp:122-177`.
- **What**: The mock is a 422-line parallel reimplementation of the 675-line production
  file, not a thin stub. Plan 069 added the schema-1 migration to **both**, character for
  character. The five new migration tests are well-constructed — they cover legacy
  0/1/2, the versioned reader-only case, and a round trip — but they exercise the
  **mock's** copy. Deleting the migration from `src/JsonSettingsIO.cpp` entirely would
  leave all five passing. Any future divergence between the two files is invisible to
  the suite, and the duplication makes divergence likely.
- **Note**: plan 069's edit to the mock was **necessary and correct**, not gratuitous —
  the mock had no `globalStatusBarPosition` handling at all beforehand, so the tests
  could not otherwise exist. The declared 3-file scope was simply too narrow. The defect
  is the test architecture, not that plan's diff.
- **Why not fixed here**: fixing it means either linking the production file into the
  host suite (it may pull in device-only dependencies — needs investigation) or
  extracting the shared parse/migrate logic into one file both sides use. Either is a
  real piece of work, not a review nit.
- **Also already diverged**, which shows the risk is not hypothetical: the mock never
  saves or loads `language` (production does, at `:380` and `:245`), and it writes a
  `doc["version"] = 1` key production does not write.
- **Status**: planned as 087 (`plans/087-settings-serializer-single-source.md`).
  Investigation confirmed the extraction is viable: the pure logic's only external
  dependency is `I18n::languageFromCode`, and `lib/I18n/I18n.cpp` is already linked
  into the host suite at `test/run_host_tests.sh:100`. The mock exists because of
  `ObfuscationUtils.h`, which the **wifi** path needs and the settings path does not.

## 2026-07-26T11:20Z — possible further zero-length `memcpy` UB in `test/mock/`

- **Found by**: claude — during plan 085
- **Where**: `test/mock/` generally; the fixed instance was `test/mock/HalStorage.h:61`
- **What**: `std::memcpy(dst, vec.data(), 0)` is UB when the vector is empty, because
  `data()` may return `nullptr` and `memcpy`'s source is declared `nonnull`. UBSan
  aborts the whole suite when this fires, producing **no** `[doctest] Status:` line — a
  silently ungated test run. The same shape may exist at other `memcpy`/`memmove` calls
  whose source is a container's `data()`.
- **Why not fixed here**: plan 085 was deliberately a one-line fix in a file every suite
  depends on; a speculative sweep was out of scope.
- **Status**: open — worth a grep next time `test/mock/` is touched, not worth a
  standalone change now.

## 2026-07-26T11:55Z — plan 095 uses `std::span`, which the device toolchain does not have

- **Found by**: claude — gating the tree before a commit
- **Where**: `src/activities/reader/SelectionModel.h:4` (`#include <span>`), with uses at
  `SelectionModel.h:97-98` and `SelectionModel.cpp:291,302,355-356`
- **What**: **The firmware does not compile.** All three firmware gates fail identically:
  `src/activities/reader/SelectionModel.h:4:10: fatal error: span: No such file or directory`.
  `platformio.ini` sets `-std=gnu++2a`, but the ESP32-C3 cross-compiler is
  **GCC 8.4.0** (`crosstool-NG esp-2021r2-patch5`), and libstdc++ did not ship `<span>`
  until GCC 10. The `-std` flag selects language rules, not library availability.
- **Why the host suite did not catch it**: `test/run_host_tests.sh` compiles with the
  *host* toolchain, which is modern and does have `<span>`. The suite is green at
  **325/325** with the firmware completely unbuildable.
- **Fix shape** (not applied — out of scope for the review that found it): replace
  `std::span<const HighlightRect>` with `const std::vector<HighlightRect>&`, or a
  `const HighlightRect*` + `size_t` pair. All six uses are function parameters, so the
  change is mechanical and local to two files.
- **Status**: resolved in the same uncommitted integration. The six span parameters
  now use `const std::vector<HighlightRect>&`, and the default ESP32-C3 firmware build
  passes with GCC 8.4.0. The compile-gate lesson below still applies.

### Ladder note — this class has now hit rung 3

"A build configuration nobody exercises" has now produced **three** failures in one day:

1. `BackgroundWifiService.cpp` — a half-applied rename in a firmware-only file the host
   suite never compiles (plan 065).
2. `ENABLE_BOOKMARKS=0` — a supported configurator profile that no environment builds
   (plan 086).
3. This one — `std::span` compiles on the host toolchain and not on the device one.

Per the ladder, **a gate is now obliged**, in addition to the tests and documentation
already added. The minimum viable gate is a pre-commit or CI leg that runs one real
firmware build (`./scripts/pio-locked.sh run -e default`) — items 1 and 3 would both
have been caught by that alone. `.github/workflows/feature-matrix-test.yml` is the
natural home for the compile-gate matrix that catches item 2. Note the **pre-push** hook
already builds firmware, so this class is caught on push but never before commit — which
is precisely why three of them accumulated in an unpushed tree.

## 2026-07-26T09:41Z — `AnnotationStore::add()` reports success before persistence is known
- **Found by**: codex — during plan 095
- **Where**: `src/util/AnnotationStore.cpp:74`, `src/util/AnnotationStore.cpp:151`
- **What**: `add()` mutates the in-memory annotation vector, calls a `void`
  `saveToFile()`, and then returns `true` even when opening, flushing, or renaming the
  annotation file failed. The selection UI therefore cannot provide truthful save
  failure or guarantee no mutation on failed persistence.
- **Why not fixed here**: out of scope for the delegated plan 095 slice, whose owned
  files explicitly excluded `AnnotationStore` and annotation persistence/format work.
- **Status**: open
