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
- **Status**: open — re-confirmed present at `39072d6de` (`grep -rn
  "MenuAction::TOGGLE_BOOKMARK" src/` still returns the push site and no handler).
  Needs on-device confirmation that the row appears and is inert before deciding
  between deleting the row or wiring it up.

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
  Re-checked at `39072d6de`: `grep -n BOOKMARKS .github/workflows/feature-matrix-test.yml`
  still returns nothing. The break of 2026-07-26 was fixed (plan 086, `1f90af803`); the
  hole that let it exist is not.
- **CORRECTION 2026-07-26T13:47Z — the headline claim above is WRONG, and the real gap is
  narrower.** An operator run of all six CI matrix configurations (from a clean worktree at
  `e56d83715`) resolved each profile's derived flags and built it. The `lean` profile
  produces exactly `EPUB=0 MD=0 XTC=0 BOOKMARKS=0 ANNOT=0` and **built successfully in
  238 s**. So CI *does* exercise `ENABLE_BOOKMARKS=0`, and this entry's premise was false.
  The mechanism I had missed: `ENABLE_BOOKMARKS` is **not** a configurator feature — it is
  absent from `platformio.ini` and from `generate_build_config.py`'s 104-entry `FEATURES`
  table. It lives only in `include/FeatureFlags.h`, defaulting to 1 (`:378-379`) and
  **auto-disabling** (`:505-511`) when none of `ENABLE_EPUB_SUPPORT` / `ENABLE_MARKDOWN` /
  `ENABLE_XTC_SUPPORT` is on. `lean` enables no book format, so bookmarks switch off
  derivatively.
  **The actual gap** is scheduling, not coverage: `.github/workflows/feature-matrix-test.yml:28`
  gates the whole workflow behind `if: github.event_name != 'pull_request' ||
  contains(labels, 'full-matrix')`, with a nightly `cron: 0 2 * * *`. It therefore never
  runs on an ordinary push, and never locally — which is why the break survived a day in an
  unpushed branch. Full matrix result: **6/6 SUCCESS**, sizes 2.90 MB (lean) → 6.03 MB (full).
- **Status**: open, but re-scoped — the fix is to make the matrix reachable before push (or
  add one gate-off leg to the always-on CI), not to add bookmarks coverage that already
  exists.

## 2026-07-26T14:10Z — plan scopes keep missing the *second consumer* of a changed file

- **Found by**: claude — reviewing plan 087's diff
- **Where**: planning practice; instances at `test/mock/JsonSettingsIO.cpp` (plan 069) and
  `scripts/generate_configurator_settings_schema.py:123-130` (plan 087)
- **What**: Twice in one wave, a plan declared a file scope that was too narrow, and the
  executor had to edit an undeclared file to make the work compile or link. Both edits were
  **necessary and correct**, so neither is a violation by the executor — the defect is in
  how the scope was derived. Plan 069 missed that the host suite links a parallel mock of
  the file being changed. Plan 087 missed that
  `generate_configurator_settings_schema.py` compiles its own exporter binary that links
  `test/mock/JsonSettingsIO.cpp`, so extracting logic into a new `.cpp` breaks that link
  until the new file is added to its source list.
- **Root cause**: the scope was derived by grepping for the *symbol* being moved, not for
  every build script that *links the file*. Those are different searches and the second one
  is the one that matters for extractions.
- **Fix shape**: before writing a plan's scope section for any file move/extraction, run
  `grep -rn "<basename>" scripts/ test/ .github/` and add every hit to the declared scope.
  Both misses would have been caught by that one command.
- **Status**: open — **2nd occurrence of this class, so per the ladder it earns
  documentation now** (this entry). A third occurrence obliges a gate; the natural one is a
  checklist item in the plan template.

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
- **Status**: open — planned as 087 (`plans/087-settings-serializer-single-source.md`),
  dispatched 2026-07-26, **did not land**. Verified at `39072d6de`: no
  `src/SettingsSerializer.{h,cpp}`, no reference to one in `run_host_tests.sh`, mock and
  production still diverged as described. It was dispatched against a tree that did not
  compile (the `std::span` break below), which its STOP conditions did not anticipate.
  Safe to re-dispatch now that the tree builds.
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
- **Status**: fixed in `1f90af803` (plan 088). The six span parameters now use
  `const std::vector<HighlightRect>&`, `#include <span>` is gone, and the default
  ESP32-C3 firmware build passes with GCC 8.4.0. Re-verified at `39072d6de`:
  `grep -rn "std::span\|include <span>" src/ lib/` is empty. The compile-gate lesson
  below still applies.

### Ladder note — this class has now hit rung 3

"A build configuration nobody exercises" has now produced **three** failures in one day:

1. `BackgroundWifiService.cpp` — a half-applied rename in a firmware-only file the host
   suite never compiles (plan 065).
2. `ENABLE_BOOKMARKS=0` — a supported configurator profile that no environment builds
   (plan 086).
3. This one — `std::span` compiles on the host toolchain and not on the device one.

Per the ladder, **a gate is now obliged**, in addition to the tests and documentation
already added. But the useful finding is that **the gate already exists and was
bypassed**, so "add a build gate" is the wrong prescription:

- `scripts/hooks/pre-commit:236-297` runs a real firmware build — the `full` profile via
  `uv run pio run -e custom` — before every commit, and flocks
  `/tmp/crosspoint-pio-build.lock` so it serialises with manual builds.
- It is **staleness-proof by construction**: results are cached under
  `.cache/build-results/<profile>-<git write-tree>`, keyed by the *staged tree OID*. Any
  change to staged content changes the key, so a green result can never be reused for a
  tree it did not build. Failures are cached too, so a known-bad state fails fast.
- That design would have caught items 1 and 3 on the commit that introduced them. It
  would **not** catch item 2 — the `full` profile has `ENABLE_BOOKMARKS` on. The
  `feature-matrix-test.yml` leg is still separately required.

The hole is `git commit --no-verify`, which the project's own fast-path workflow
recommends (hand-run the gates, then commit unverified, because a manual `pio run` does
not prime the hook's cache and the hook would rebuild for ~3 minutes). That is exactly
how the `std::span` break reached a commit: the gate battery was run at 11:36 and plan
095's files landed at 11:44, so the hand-run "gates" described a tree that no longer
existed. The hook's `write-tree` key exists precisely to make that mistake impossible,
and bypassing it discards the protection.

**So the rung-3 action is one of:**
1. Stop using `--no-verify`, and instead make the hook's build cheap to pre-warm — e.g.
   a `scripts/prime-precommit.sh` that stages, computes `git write-tree`, builds, and
   writes the cache file the hook will look for. This keeps a single source of truth for
   "was this exact tree built?".
2. Or, if `--no-verify` stays, add a staleness check that refuses it when any staged file
   is newer than the last gate log — the cheap version of the same idea.

Option 1 is strictly better: it removes the reason to bypass rather than policing the
bypass.

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

## 2026-07-26T14:00Z — `user_fonts` key in `FeatureCatalog.cpp` is orphaned with no route handler

- **Found by**: agy — during plan 094
- **Where**: `src/core/features/FeatureCatalog.cpp:101`
- **What**: `FeatureCatalog.cpp` registers a `"user_fonts"` feature entry (hard-coded `enabled=false`, no callback), but no `WebRouteRegistry` or server handler references `"user_fonts"` or `UserFontsApi`. The real font routes `/api/fonts`, `/api/fonts/upload`, and `/api/fonts/delete` are registered unconditionally.
- **Why not fixed here**: out of scope for plan 094 (scope was documentation files under `docs/`).
- **Status**: open


## 2026-07-26T15:20Z — incremental selection overlay restores more than it repaints

- **Found by**: claude — targeted review of plan 095 (damage-rect math)
- **Where**: `src/activities/reader/SelectionModel.cpp:383-394`, with the coordinate
  mapping at `:258-283`
- **What**: `applyIncrementalSelectionOverlay` computes its restore region and its repaint
  set in **different coordinate spaces**, and the restore region is the larger of the two.
  - The restore loop copies whole bytes, `firstByte = x0 / 8` through `lastByte = x1 / 8`
    (`:383-389`). That covers physical x from `firstByte * 8` to `lastByte * 8 + 7` — i.e.
    the damage box **rounded outward to byte boundaries, up to 7 px wider on each side**.
  - The repaint filter then tests `highlightRectsIntersect(rect, damage)` (`:391`) using
    the **logical, un-expanded** damage rect.
  - So a run inside that 7 px margin is restored to base (its highlight erased) and then
    fails the intersection test, so it is never re-inverted.
- **Why the axis matters**: `toPhysicalBounds` maps physical x from **logical y** in
  `Portrait` (`:260-263`) and `PortraitInverted` (`:272-275`). That is the axis along which
  text lines stack, so the 7 px slop runs between adjacent lines' runs. In the two
  landscape orientations physical x comes from logical x (`:266-267`, `:278-279`), where
  the slop runs along the text direction and there is only one run per line, so it is
  far less likely to bite.
- **Reachability**: `buildHighlightRuns` emits one rect per line, sized to glyph extent
  (`maxY - minY`, `:227`), not to the line box — so the vertical gap between consecutive
  runs is the leading, commonly under 8 px at normal line spacing. The orientation passed
  is the live renderer orientation (`EpubReaderActivity.cpp:2890`), so all four cases
  occur. The `runsOverlap` guard (`:358`) rejects *overlapping* runs but not *adjacent*
  ones, which is precisely the case here.
- **Symptom**: extending a selection across a line boundary in portrait leaves a 1–7 px
  horizontal band of an adjacent highlighted line un-inverted. It persists until something
  forces a full redraw. Cosmetic, not a crash or a correctness bug in the selection itself.
- **Confidence**: the space mismatch is **certain** — it is visible in the two cited lines.
  Whether it is observable on any given page depends on a run landing within the 7 px
  margin. **Not reproduced on device or in a test**; found by reading.
- **Fix shape**: make the repaint set match what was actually restored. Simplest correct
  option is to drop the `highlightRectsIntersect` filter and re-invert **all** of
  `currentRuns` — runs are few (one per selected line), `invertHighlightRect` already
  clamps and no-ops on empty rects, and the filter is a micro-optimisation. The
  alternative — expanding `damage` to the byte-aligned physical region before testing — is
  more code for the same result and re-introduces the risk of the two spaces drifting apart
  again.
- **Regression test**: a host test can catch this without hardware. Build two adjacent
  non-overlapping runs whose logical-y gap is < 8 px, call the overlay in `Portrait`, and
  assert the unchanged run's pixels are still inverted afterwards. That test fails today.
- **Why not fixed here**: this review was read-only, and the reviewer does not modify
  source. Also note the gates cannot catch it — it compiles cleanly, the host suite is
  333/333, and all six matrix configurations build. This is exactly the class of defect
  that survives a green build.
- **Status**: **fixed in `e5030d70f`** (plan 096). The repaint set is now derived from the
  physical span actually restored (`[firstByte*8, lastByte*8+7]` x `[y0,y1]`), so there is a
  single source of truth for "what was restored". Regression test covers all four
  orientations and was written before the fix; operator independently reverted the fix while
  keeping the test and confirmed `[doctest] Status: FAILURE!`, 1 failed.
- **CORRECTION 2026-07-27 — this entry's orientation analysis was WRONG.** It predicted the
  landscape orientations were "far less likely to bite" because their physical x derives from
  logical x rather than logical y. In fact **all four orientations fail** without the fix
  (4 `ERROR: CHECK` lines in the red-proof run). The byte-alignment slop is not specific to
  the portrait axis mapping, so the defect was broader than described. The portrait reasoning
  was a plausible-sounding narrowing that the test disproved — a reminder that a mechanism
  story is not evidence, and that writing the failing test first is what actually settles
  scope.

## 2026-07-26T15:20Z — plan 095's allocation-free claim VERIFIED (no defect)

- **Found by**: claude — targeted review of plan 095
- **Where**: `src/activities/reader/SelectionModel.h:95-98`,
  `src/activities/reader/EpubReaderActivity.cpp:2547-2548,2600-2601`
- **What**: recorded as a **negative** result so nobody re-audits it. The header claims
  "Once `runs` has been reserved for the current page, warm cursor moves do not allocate."
  That claim holds: the reusing overload calls `runs.clear()` (`:196`), which preserves
  capacity, then only `push_back`s (`:226`); and both caller-owned vectors are reserved to
  `selModel.words.size()` at `EpubReaderActivity.cpp:2547-2548` and `:2600-2601`, which is
  an upper bound on run count (runs ≤ words). So no reallocation occurs on a cursor move.
- **Status**: wontfix (no defect) — verified correct 2026-07-26

## 2026-07-29T14:40Z — WiFi picker's scan-retry budget was spent in the same loop() tick (2nd occurrence of this class)

- **Found by**: claude — ad hoc, during on-device bring-up of `claude_bridge`
- **Where**: `src/activities/network/WifiSelectionActivity.cpp` (`processWifiScanResults`,
  `WIFI_SCAN_FAILED` branch) and `src/activities/network/WifiSelectionActivity.h`
  (`SCAN_RETRY_MAX`)
- **What**: on a real X4 the picker logged `WiFi scan failed 4 times; showing empty list`
  **instantly** on entry, with three saved networks in range. The first scan correctly goes
  through `startWifiScan()`, which does `WiFi.disconnect(false, true)` and then arms
  `RadioStep::ScanReset` with a 100 ms settle. The retries did neither — they called
  `startWifiScanAsync()` directly from the failure handler, so all three ran in consecutive
  `loop()` ticks within single-digit milliseconds, every one of them while the SDK NVS
  auto-connect that aborted the first scan was still in flight. The retry budget existed but
  could not do anything; the comment at the trap already said "retry silently before showing
  an empty list", which is why it read as already-handled.
- **Why it matters beyond the picker**: an empty list makes a device with saved credentials
  look unprovisionable, and the background web server never comes up, so every network
  feature (file transfer, settings, OTA, OPDS) is dark until the user retries by hand.
- **Class history**: this is the **2nd** occurrence of "SDK auto-connect poisons the first
  scan after STA power-up". The 1st produced the suppression in `startWifiScan()` plus the
  retry counter. Per the ladder, 2nd occurrence requires **documentation of why it keeps
  recurring, at the trap and in the docs** — hence the expanded comment on `SCAN_RETRY_MAX`
  and this entry. The recurring reason: the suppression and the retry live in *different*
  code paths, so it is easy to add a retry that silently skips the suppression. A future
  3rd occurrence escalates to an automatic gate.
- **Status**: **fixed (uncommitted)** — retry decision extracted to
  `wifi_entry::evaluateScanRetry` (`src/network/wifi/WifiEntryPolicy.{h,cpp}`) returning an
  exponential backoff (150/300/600 ms), and the failure branch now re-arms the radio and
  goes back through `RadioStep::ScanReset`. Covered by 4 host cases in
  `test/host/test_wifi_entry_policy.cpp`, including one asserting every retry carries a
  non-zero delay — the zero-delay retry *is* the bug.

## 2026-07-29T15:20Z — device does not rejoin WiFi at boot with Background Server = Always

- **Found by**: claude — ad hoc, during on-device bring-up of `claude_bridge`
- **Where**: boot-time auto-connect path — `BackgroundWifiCoordinator::reconcile` /
  `background_server::evaluateAutoConnect` (`src/network/background/`), not the foreground
  picker.
- **What**: on an X4 with **Settings > Net > Background Server = Always** and a saved,
  in-range credential (`Arrakis`, connected manually moments earlier so
  `lastConnectedSsid` is set), a reboot leaves the device off the network
  indefinitely. Observed: no UDP discovery reply on 8134, no HTTP on the previous lease,
  and a full `192.168.86.0/24` port-80 sweep finds no new host, polled to 90 s and
  re-checked minutes later. The device was sitting on a settled Home screen (cover
  rendered, so `HomeActivity::blocksBackgroundServer()` is false) and answered serial
  `CMD:PING` throughout, so the firmware was healthy — it simply never associated.
  Connecting by hand through Settings > Wi-Fi Networks works immediately and the
  background server then starts.
- **Not diagnosed**: the mechanism was not established. Candidates worth checking are
  the `skipCount` backoff and `waitingForNewCredential` inputs to `evaluateAutoConnect`,
  and whether anything ever calls it in this state. Do not assume this is the same root
  cause as the picker's scan-retry bug (fixed in `44a360c8a`) — that fix is in the
  foreground picker and is confirmed working; this path is separate and still broken.
- **Why it matters**: any feature that depends on the background web server (file
  transfer, settings, OTA, OPDS, `claude_bridge`) is dark after every reboot until the
  user manually visits the WiFi screen. "Always" reads as a promise the device does not keep.
- **Why not fixed here**: out of scope — that session's scope was the `claude_bridge`
  feature and the foreground scan-retry fix.
- **Status**: **root-caused 2026-07-29 — it is heap, not auto-connect logic.**
- **CORRECTION 2026-07-29T18:05Z — the "not diagnosed" candidates above were both wrong.**
  Neither the `skipCount` backoff nor the `waitingForNewCredential` latch is responsible.
  With developer-mode logging on, the boot log says plainly:
  `[WRN] [BGWIFI] bg server deferred: low heap (40556)` / `bg server backoff: waiting 30s`,
  repeating every 30 s forever. `BackgroundWifiService::canStartNow()` requires
  `MIN_START_HEAP_BYTES = 60000` (`src/network/background/BackgroundWifiService.h:55`), and
  measured free heap with **any** activity resident is 35–52 KB — so the gate is
  unreachable by construction and the retry can never succeed. Two hypotheses were
  advanced and disproved before this: Home's 48 KB cover snapshot (disproved — Settings
  holds no snapshot and settles just as low, 37.3 KB vs 40.7 KB) and the credential latch
  (a real one-way-door bug, but not this symptom). The lesson is the same one this file
  keeps recording: a mechanism story is not evidence. One `LOG_WRN` promoted out of
  developer-mode-only visibility would have answered it in a minute — the silent skip is
  itself the defect that made this expensive.
- **Related, still open**: the true requirement is ≈41.5 KB, not 60 KB — route setup
  consumes 29,260 B measured (`40,556 → 11,296`) against a
  `WEB_SERVER_MIN_SAFE_HEAP_BYTES = 12288` floor
  (`src/network/server/CrossPointWebServer.cpp:65`). Free heap between activities is
  ~88 KB with a 64 KB contiguous block, and nothing currently attempts a start in that
  window.

## 2026-07-29T18:50Z — linker-map section listings over-count static RAM; the "duplicate upload session" cost 0 bytes

- **Found by**: claude — ad hoc, during a heap-reduction pass on the background server
- **Where**: `src/network/server/UploadApi.cpp`, `src/network/server/FileRoutes.cpp`,
  `src/network/server/CoreWebRoutes.cpp`; method issue applies to any map-file audit
- **What**: a linker-map scan reported the two largest static allocations in the firmware
  as two ~6,344 B HTTP upload sessions — `network::sharedBufferedHttpUploadSession`
  (`BufferedHttpUpload.cpp`) and `uploadSession` (`UploadApi.cpp`) — and concluded ~6.3 KB
  was recoverable by deduplicating them. **A controlled build pair disproves this.** With
  the duplicate present and with it removed, `.dram0.data` + `.dram0.bss` are byte-identical
  at 19,356 + 108,096 = 127,452 B (`riscv32-esp-elf-size -A` on the same ELF whose map was
  counted). The saving is **0**.
- **Why**: `CoreWebRoutes` / `FileRoutes` / `UploadApi` are referenced **only** by the host
  test harness (`test/host_server/main.cpp:120`, `test/host_server/routes/files.cpp:4`) and
  by nothing in the firmware. On device the whole path is unreachable, so `--gc-sections`
  discards `uploadSession`'s `.bss` and it never occupied DRAM.
- **The generalisable trap**: a symbol appearing in the map's *input section* listing does
  not mean it occupies RAM in the linked image. Only allocated output sections count.
  Any per-object static-RAM attribution derived from map input sections will over-count
  every garbage-collected section, and can reconcile to the correct grand total while
  individual line items are wrong. Verify a claimed saving with a **build pair measured by
  `size -A` on the ELF**, never by symbol presence in the map.
- **Secondary finding (open)**: `CoreWebRoutes` / `FileRoutes` / `UploadApi` are firmware
  dead code kept alive only by the host harness, i.e. the host tests exercise an upload
  implementation the device never runs. That divergence is a silent-drift risk regardless
  of RAM.
- **Status**: measurement corrected; the dedupe was kept anyway (removes ~376 lines of
  duplicate logic and makes the harness share the firmware implementation), but it is a
  maintainability change, **not** a RAM change. Dead-code question open.

## 2026-08-10T15:45Z — PNG sleep images fail on a fragmented heap: the guard checks free heap, the allocation needs one contiguous block
- **Found by**: claude — ad hoc (device verification of the device-first Terminus sleep flow)
- **Where**: `lib/Epub/Epub/converters/PngToFramebufferConverter.cpp:278-289` (and the same pattern at :316)
- **What**: `sizeof(PNG)` is 59,456 bytes, allocated as a single `new (std::nothrow) PNG()`. The
  guard above it (`MIN_FREE_HEAP_FOR_PNG`) tests `ESP.getFreeHeap()` — *total* free heap — so it
  passes whenever ~75 KB is free in aggregate, even when no 58 KB contiguous block exists. Observed
  on device entering sleep with `free=91972 largest=36852`: the guard passed, the allocation
  returned nullptr, and `SleepActivity::renderImageSleepScreen` fell back to
  `renderDefaultSleepScreen()`. User-visible effect: the Terminus dashboard (and any PNG sleep
  image) silently degrades to the default sleep screen after the web server has run and fragmented
  the heap. The guard should test `ESP.getMaxAllocHeap()`; separately, a 58 KB contiguous
  requirement at sleep time is probably the wrong design — pre-converting the fetched Terminus PNG
  to a 1bpp framebuffer at fetch time would remove the sleep-path allocation entirely.
- **Why not fixed here**: out of scope for the Terminus setup/sleep-option work (scope was
  `src/features/terminus_sleep/`, `src/SettingsList.h`, settings/sleep wiring). Changing the PNG
  decoder's admission control affects every image path (covers, Pokedex, custom sleep art) and
  needs its own measured verification.
- **Correction (2026-08-10)**: the "renders weird" symptom is NOT this bug. The pinned
  `/sleep/trmnl_latest.png` was pulled off the device and inspected: it is an 862-byte 480x800
  1-bit PNG containing the TRMNL "zzz" sleep placeholder, i.e. exactly what the BYOS returned. The
  firmware fetched, pinned and drew it correctly. The heap-guard defect below is still real — it
  was observed failing once at sleep entry — but it is intermittent (it needs a fragmented heap)
  and it is not what the user sees day to day. Scope this finding to the guard only.
- **Status**: open

## 2026-08-10T15:45Z — Connect settings screen logs missing definitions for ankiConnectUrl / ankiConnectDeck
- **Found by**: claude — ad hoc (device verification of the device-first Terminus sleep flow)
- **Where**: serial: `[ERR] [SET] Missing connect setting definition for key=ankiConnectUrl`
- **What**: Entering Settings emits two `[ERR] [SET] Missing connect setting definition` lines, for
  `ankiConnectUrl` and `ankiConnectDeck`. The Connect screen references keys that
  `getSettingsList()` does not emit under the current feature flags, so either the topic list or the
  settings definitions are stale. Logged as ERR on every Settings entry.
- **Why not fixed here**: out of scope for the Terminus work; belongs to the Anki/Connect feature.
- **Status**: open

## 2026-08-11T00:00Z — Upstream absorption triage: defects found but not fixed in the absorption batches
- **Found by**: claude — full triage of 1267 crosspoint + crossink commits since merge-base `2754a5ff0`
  (see `plans/UPSTREAM_ABSORPTION_2026-08.md` for the ranked plan; per-bucket reports in the session
  scratchpad under `absorb/reports/*.md`)
- **Where / What**: each of these was verified live in our tree while triaging, but sits outside the
  three batches that landed (`8963002fd`, `36a789ed7`, `5997ff697`):
  - `src/network/ota/OtaUpdater.cpp` + `lib/JsonParser/ReleaseJsonParser` — **OTA from our own GitHub
    releases installs unverified.** We stream and verify sha256 (`parseSha256Hex`,
    `calculatePartitionSha256`) but only on the feature-store bundle path, from `bundles[].checksum`.
    `ReleaseJsonParser` has no `sha256`/`digest` member at all, so the GitHub-release path has no hash to
    check against. Upstream `0dd6d0ee5` parses GitHub's `digest: "sha256:<hex>"` asset field.
  - `src/network/ota/FirmwareFlasher.cpp:116` — **no chip-family validation.** Only the header magic byte is
    checked, so flashing an X3 image onto an X4 bricks to a boot loop. Both upstream (`e00f5958`) and
    crossink (`6de9409d2`) fixed this independently; chip_id sits at `esp_image_header_t` offset 12 and can
    be compared against the running partition's own, no chip enumeration needed.
  - `lib/OpdsParser/OpdsParser.cpp` — no cap on entry count or on title/href/id string length. A large
    Booklore/Grimoire feed can exhaust the heap. (Our Grimoire server is exactly this shape.)
  - `src/activities/browser/OpdsBookBrowserActivity.cpp:394` — `requestUpdate(true)` per HTTP chunk, and
    `ActivityManager::requestUpdate` does `xTaskNotify(..., eIncrement)`, so a download queues one e-ink
    repaint per chunk. Our download path also never drops `WIFI_PS_MIN_MODEM` the way `OtaUpdater.cpp:816`
    does.
  - `src/MappedInputManager.cpp:152-157` — reader *menus* ignore orientation while page turns honour it.
    Carries the verbatim pre-fix comment. `ENABLE_GLOBAL_LANDSCAPE` makes this worse for us than upstream.
  - `src/main.cpp` — no `powerButtonReleasedSinceWake` guard, so the wake-hold release edge feeds straight
    into the FORCE_REFRESH / double-tap paths.
  - `sanitizeFilename` — front-truncates, so a long-titled upload loses its `.epub` extension.
  - `src/network/HttpDownloader.cpp:455` — `addHeader("User-Agent", …)` is a silent no-op (Arduino
    `HTTPClient` drops that header); we ship as `ESP32HTTPClient`.
  - `src/util/BookCacheUtils.cpp` — no `clearBookCachePreservingUserState`, so clearing an XTC or TXT book's
    cache still destroys its reading data. Batch 3 fixed the EPUB path only (`Epub::clearRenderCache`); the
    Xtc/Txt equivalents remain.
  - `drawOptionPopup` — no windowing, so a long popup overflows the screen.
  - `CrossPointSettings` has no mutex (upstream `fd43ca2fe`, mislabelled `chore`). We run concurrent
    FreeRTOS tasks that touch settings (web server, background WiFi, Terminus fetch, OTA worker); a save
    racing a load corrupts settings.json.
  - `SettingsActivity.cpp:369` activates on `wasReleased(Confirm)` while `FontSelectionActivity.cpp:122`
    finishes on `wasPressed(Confirm)`, so the release leaks to the parent and the font picker reopens
    immediately after every font choice.
  - `EpubReaderActivity.cpp:1841-1846` divides the already-clamped `currentPage` by the old page count on
    the reflow-restore path, and the buggy snippet is duplicated at `:935`, `:952`, `:1555`, `:1597` —
    `:1555` being `applyOrientation()`, so **global-landscape rotation loses the reading position the same
    way a font-size change does**.
  - `EpubReaderActivity.cpp:1455` clears every bookmark in the book with no confirmation, from a menu row
    adjacent to "View bookmarks".
  - `performDeferredSilentIndexing()` (`:1984-2023`) runs a full chapter layout with no heap gate.
- **Why not fixed here**: the absorption batches were scoped to fixes that were isolated, verifiable
  in-place, and cheap to gate. These each need their own scoped change — several need a new i18n string, a
  settings-schema regeneration, or a `SECTION_FILE_VERSION` bump, and the OTA ones need on-device
  verification before they can be trusted.
- **Status**: open

## 2026-08-12T00:00Z — /releases/latest 404s: every published release is a prerelease
- **Found by**: claude — pre-flight for the OTA digest work (`d731c8fd5`), querying the live GitHub API
- **Where**: `src/network/ota/OtaUpdater.cpp:24` `stableReleaseUrl` →
  `https://api.github.com/repos/Unintendedsideeffects/ForkDrift-crosspointReader/releases/latest`
- **What**: that endpoint returns `{"message":"Not Found","status":"404"}`. GitHub's `/releases/latest`
  excludes prereleases and drafts, and both published releases (`nightly`, `latest`) have
  `prerelease=true`. So the stable channel's first candidate URL always fails; it only works at all
  because `checkForUpdate()` walks a candidate list and falls through to the tag-based URLs.
- **Also noted**: the `latest` release carries both `crosspoint-standard.bin` and
  `firmware-20260726-39072d6.bin` with an identical sha256 — the same file published under two names.
  Only the `firmware-*.bin` form satisfies `isFirmwareAssetName()`, so the duplicate is inert, but it
  doubles the release payload.
- **Why not fixed here**: out of scope for the digest work, and the fix is a release-publishing decision
  (mark a release non-prerelease, or drop the `/releases/latest` candidate) rather than a firmware change.
  `scripts/release.sh` publishes locally, so this is a one-line change wherever the prerelease flag is set.
- **Good news from the same check**: GitHub returns `digest: "sha256:<hex>"` on *every* asset of both
  releases, computed server-side. That is what made fail-closed verification safe to adopt.
- **Status**: open

## 2026-08-12T12:00Z — state.json has no write lock (the deferred half of fd43ca2fe)
- **Found by**: claude — while porting the `CrossPointSettings` mutex (`c80117561`)
- **Where**: `src/CrossPointState.cpp:81` `CrossPointState::saveToFile()` / `:86` `loadFromFile()`
- **What**: same defect class as the settings one just fixed. `saveToFile()` calls
  `JsonSettingsIO::saveState()` with no serialisation, so two tasks saving state concurrently can
  interleave and leave a truncated or spliced `state.json`. Upstream fixed both files in the same
  commit; only the settings half was ported.
- **Why not fixed here**: `CrossPointState` already carries a `PendingStateLock` (`:21`, a FreeRTOS
  semaphore RAII guard) protecting `pendingOpenPath` and friends. That is a *different* concern from
  the file, and adding a second lock beside it needs its own reasoning about whether any path holds
  `PendingStateLock` across a save — otherwise the fix introduces a deadlock instead of preventing a
  corruption. Folding that into a commit about settings would also have made the settings regression
  non-bisectable.
- **Shape of the fix**: same as `c80117561` — a file-static `std::mutex` taken in `saveToFile()` and,
  scoped, around the `loadState()` parse. `CrossPointState::saveToFile()` has no router indirection,
  so it is simpler than the settings case; the only real work is auditing the four `PendingStateLock`
  call sites (`:100`, `:105`, `:115`, `:120`) for a save underneath.
- **Blast radius if it bites**: a corrupt `state.json` loses recent-books and sleep-image state, not
  reading progress. Lower severity than the settings case, which is why it was safe to defer.
- **Status**: open

## 2026-08-12T15:00Z — OPEN: abort() under heap starvation while reading (device-reproduced)
- **Found by**: claude — first on-device run of this session's work (X4 over USB/IP)
- **Symptom**: SYSTEM CRASH screen, `abort() was called at PC 0x421d0f89`. Reproduced twice:
  once during initial indexing, once after ~45 page turns.
- **Decoded, not guessed**: `riscv32-esp-elf-addr2line` against the matching ELF resolves the PC to
  `__cxxabiv1::__terminate` (libsupc++ `eh_terminate.cc:44`). With `-fno-exceptions` that means an
  **uncaught C++ throw** — `bad_alloc`/`length_error` from a container — not an assert or a WDT.
- **Corroborating log**: `[ERR] [PTX] OOM guard: truncating block (low heap, largest=20468)` fires
  shortly before. The heap's largest contiguous block is down to ~20KB.
- **Fixed so far (both real, neither proven to be THIS crash)** — see `7c666fe59`:
  - unguarded `rulesBySelector_.reserve(ruleCount)` in `CssParser::loadFromCache` (~6KB contiguous at
    MAX_RULES=1500), made per-section-build by the CSS release change `1b1788ada`;
  - `performDeferredSilentIndexing()` re-entering the **non-recursive** `renderingMutex` when called
    from `render()` (introduced by `81dd1f35d`).
- **Still unknown — the next experiment**: is this a regression from this session or pre-existing
  starvation on this book? Flash the pre-session baseline (`924a9c195`) and run the identical
  script: open the book, wait out indexing, then 45 × `CMD:BTN:PAGEFWD` at 4s spacing while
  streaming serial. If baseline also aborts, this is pre-existing and the CSS/async work is
  exonerated. If it does not, bisect `3f2ba5419` (rule storage) vs `1b1788ada` (release/reload) vs
  `81dd1f35d` (async overlap).
- **Specific suspicion worth testing directly**: `1b1788ada` traded steady CSS residency for
  repeated alloc/free of a bucket array plus N nodes on **every section build**. On a no-MMU heap
  that churn can fragment worse than the residency it saved — i.e. the optimization may be net
  negative. It was never measured on device; the plan called for that measurement and it has not
  been done.
- **Harness note**: screenshots and `CMD:PING` both time out while the reader is busy in a long
  build, so "no response" does NOT imply a crash. Confirm state by resetting and reading the screen.
  Note a manual esptool reset clears the crash screen, so capture it *before* resetting.
- **RESOLVED 2026-08-12T19:40Z — root cause found, and it was neither of the two fixes above.**
  The bisect was never needed. Enabling `developerMode` turns on `LOG_INF`/`LOG_DBG` at runtime
  even in a `LOG_LEVEL=0` build (`Logging.h:68-84`), so the crashing binary could be instrumented
  **without rebuilding it**. Streaming serial through the repro then captured the full stack dump,
  and decoding every code-looking word in it reconstructed the call path the wrapped
  `panic_print_backtrace` had suppressed:
  `Section::createSectionFile` → `ChapterHtmlSlimParser::parseAndBuildPages` → expat `doContent`
  → `ChapterHtmlSlimParser::endElement` → `vector<pair<int,FootnoteEntry>>::push_back`
  → `_M_realloc_insert` → `operator new` → throw → `__terminate`.
  The thrower was an **unguarded footnote-vector growth**. Each element is 132 bytes and doubling
  needs `2N × 132` contiguous while the old block is still live (~3× peak), which at the observed
  `largest=15348` tops out near 58 footnotes — and the reproduction book is footnote-dense.
  Fixed by a cap plus a heap guard, mirroring the `[PTX]` idiom. Verified by re-running the
  identical repro on the same hardware: **0 aborts**, and the guard logs
  `[CHP] Footnote guard: dropping links` at the exact moment that previously aborted.
- **Lesson (method, not code)**: the two earlier fixes were inferred from plausibility and both
  were wrong about *this* crash. The PC was decodable the whole time. Decode first, infer never.
- **Status**: fixed in `548b26251`

## 2026-08-12T19:45Z — An empty CSS parse is written to cache and is then sticky forever
- **Found by**: claude — while device-debugging the abort() above (ad hoc)
- **Where**: `lib/Epub/Epub/css/CssParser.cpp:871` (`saveToCache()`), storage bail-out at `:464`
- **What**: `processRuleBlockWithStyle()` stops storing rules when `canGrowRuleContainers()` is
  false, so a parse that runs under heap pressure can legitimately produce **zero** rules.
  `saveToCache()` then persists that empty result, and it is indistinguishable from "this book has
  no usable rules". Every later open loads `css_rules.cache` and gets 0 rules, so the book renders
  unstyled **permanently**, even once heap is healthy again. Observed live on the reproduction
  book: `[CSS] Loaded 0 rules + 0 descendant rules from cache` on every single open, for a Packt
  technical EPUB that certainly ships real CSS.
- **Not yet proven**: that this particular cache was written by a degraded parse rather than a
  genuinely empty stylesheet. The decisive test is cheap — delete `css_rules.cache` for the book,
  reopen with healthy heap, and see whether rules appear.
- **Why not fixed here**: scope was the footnote-vector abort in `ChapterHtmlSlimParser`. The fix
  is also a design choice, not a one-liner: either refuse to cache a degraded parse, or record a
  "degraded" flag in the cache so it can be retried later.
- **Status**: open

## 2026-08-12T19:45Z — Reading this book costs 18 silent heap-defrag restarts in 7 minutes
- **Found by**: claude — during the abort() verification run (ad hoc)
- **Where**: `src/activities/reader/EpubReaderActivity.cpp` (`heapDirtyFromIndexing_`), `src/main.cpp`
- **What**: with the abort fixed, a 45-page-turn run completes without crashing but logs
  `[MAIN] Silent restart (target=1)` **18 times**. The safety net works — position is preserved and
  the user may not notice — but it is firing constantly because the heap is genuinely exhausted:
  free heap sits near 40KB with the largest block down to 13-27KB, and `[PTX] OOM guard`,
  `[JPG] Not enough heap for JPEG decoder`, `[ERS] OOM: grayscale strip scratch` and
  `SELECTION_INDEX_FALLBACK` all fire repeatedly. Anti-aliasing and images are being silently
  dropped from pages.
- **Bearing on the CSS work**: this is the measurement the approved plan required and never got.
  It does not by itself convict `1b1788ada` (release/reload) — that still needs a before/after
  free-heap + `getMaxAllocHeap()` comparison on a warm open — but it does establish that on a
  CSS-heavy technical EPUB the reader has essentially no headroom.
- **Why not fixed here**: scope was the footnote-vector abort.
- **Status**: open

## 2026-08-12T20:30Z — Terminus awake-refresh path: four defects found by exercising it on hardware
- **Found by**: claude — ad hoc, driving a live X4 (192.168.86.51) against the BYOS Terminus
  server at 192.168.86.25:2300 over USB serial + HTTP
- **Where**: `src/features/terminus_sleep/Registration.cpp`,
  `src/network/background/BackgroundWebServer.cpp:123`,
  `src/network/background/BackgroundWifiService.cpp:176`
- **The path**: while awake with a background server up, `onBackgroundServerTick` sees a due
  refresh and **stops the running web server** (keeping STA) so the 12 KB fetch task has
  contiguous heap; the next `onBackgroundNetworkReady` performs the fetch, then the server
  restarts. Verified working end to end: manifest → image → pin, ~900 ms total.

**1. `waitForFetchTask` blocks the main loop for up to 120 s, but only in one of the two
   background-server modes.** `TRMNL_FETCH_WAIT_CAP_MS = 120000` (`:45`) and `waitForFetchTask`
   spins `delay(50)` (`:400-410`). `onBackgroundNetworkReady` has **two** dispatch sites:
   `BackgroundWifiService.cpp:176` runs on the BGWIFI task (harmless), but
   `BackgroundWebServer::startServer()` is reached from `backgroundServer.loop()` in the
   **main loop** (`main.cpp`). So with Background Server = "Only on Charge", a slow or hung
   Terminus server freezes input and rendering for up to two minutes. Measured on the
   on-charge path against a healthy sub-second LAN server: main-loop stall of **1956 ms vs a
   67 ms baseline** (probed with `CMD:PING`, which is serviced from the main loop). The 120 s
   figure is the bound from the constant, not an observed value — but nothing caps it lower,
   and a hung TCP connect is exactly what that cap is for.

**2. `fetch_running` is unobservable and the three 409 "fetch in progress" guards are
   effectively dead.** The fetch only ever runs while the web server is stopped — that is the
   entire point of the recycle. So an HTTP client polling `/api/terminus/status` during a fetch
   gets a **connection failure**, never `fetch_running: true`; confirmed by polling every 5 s
   across a full forced fetch (never once observed true). The 409 branches at `:470`, `:541`
   and `:554` can therefore only fire after `waitForFetchTask` has *timed out*, i.e. only in the
   pathological case. Consequence: `/api/terminus/test` returns 202 and then the device drops
   off the network, so the web UI can never show progress or a result.

**3. A failed fetch sets no backoff.** `fetchTaskRetryAfterMs` is assigned only when
   `xTaskCreate` fails (`:370`). When the fetch itself fails, `terminusFetchTask` calls
   `recordFetchAttempt(false)` and sets nothing, so retry pacing falls entirely to
   `refreshDue()`'s `kFailureRetryIntervalS` — which requires a usable wall clock.

**4. With an unusable clock, a failing fetch re-arms on every server start.**
   `recordFetchAttempt` updates `trmnlLastAttemptEpoch` only when `wallclock::clockUsable()`
   (`:344`). With no NTP, a failed attempt leaves it at 0, and `terminusFetchDue(true)` returns
   `allowUnsetClock && trmnlLastAttemptEpoch == 0` → true forever. `onBackgroundNetworkReady`
   passes `allowUnsetClock=true`, so every server start refetches. What stops this becoming a
   hot loop is only that the *tick* passes `false` and so never recycles — the two call sites
   disagreeing is load-bearing, undocumented, and one edit away from a request storm.

**Also worth noting (by design, but user-visible)**: every due refresh tears down and rebuilds
   the web server, WebSocket and mDNS. Any open browser session or WebSocket drops. At the
   observed 300-900 s server interval that is dozens of drops per day.

- **Why not fixed here**: the ask was to exercise the path and report. Fixes involve real design
  choices (where the blocking wait belongs, whether status should survive the recycle).
- **Status**: all four fixed in `a73e57f58`, verified on device.

## 2026-08-12T21:10Z — The Terminus boot fetch always fails: no heap for the task at that point
- **Found by**: claude — surfaced by the `last_stage` instrumentation added in `a73e57f58`
- **Where**: `src/features/terminus_sleep/Registration.cpp` (`startFetchTask`),
  `src/network/background/BackgroundWebServer.h:74` (`MIN_FREE_HEAP_TO_START = 76000`)
- **What**: on every boot the first Terminus fetch fails immediately with
  `[TRMNL] Failed to create fetch task` — `xTaskCreate` cannot get the 12 KB stack, because
  free heap at that instant is **6800 bytes** (the same log line shows
  `Library shelf refresh skipped: low heap (6800, need 84000)`). Status then reports
  `last_stage: "starting"`, `last_fetch_ok: false`.
- **Why this matters beyond one wasted attempt**: the design's premise is that the pre-server
  window has *more* contiguous heap than after the route-heavy server allocates. At boot that
  is false — heap there (6800) is far worse than once the server is up (41652 observed
  moments later). So the one window the feature deliberately chose is the worst one available
  on the first pass.
- **User-visible consequence**: the failed attempt now arms the 60 s backoff added in
  `a73e57f58` (correctly), and `onBackgroundServerTick` refuses to recycle while
  `fetchTaskRetryActive()`. So pressing "Test" in the plugin UI within a minute of boot is
  accepted with 202 and then does nothing for up to 60 s. Reproduced.
- **Also observed alongside**: with Background Server = "Only on Charge", the server could not
  start at all for minutes — steady free heap ~43 KB against `MIN_FREE_HEAP_TO_START = 76000`.
  That threshold may simply be unreachable in current steady state; worth measuring separately.
  While in that state `CMD:SETTINGS` is also refused (its own 48 KB floor), so the device
  cannot be reconfigured over serial without a reboot first.
- **Resolution**: preflight `heapguard::canAllocate` and defer, per the maintainer's call that
  waiting for full boot is normal device behaviour. The defect was never that the fetch cannot
  run at boot — it is that a *predictable* shortage was recorded as a failed attempt
  (`last_fetch_ok = false`, `have_attempted = true` for a server never contacted) and punished
  with the failure backoff. Now: no attempt recorded, `last_stage = "deferred-low-heap"`, only
  the retry timer armed (still required, or the tick recycles the server every second on a
  device that never has the heap). Guard ordering extracted as
  `terminus_refresh::classifyStart` and host-tested.
- **Status**: fixed in `5bde680d7`, verified on device (defers at boot, succeeds later).

## Resolved from that session
The `MIN_FREE_HEAP_TO_START = 76000` threshold was not a heap ceiling but a stale gate: the
two background-server modes were running different admission rules. `BackgroundWifiService`
("Always") had already been migrated to `BackgroundServerPolicy`'s derived,
fragmentation-aware gate; `BackgroundWebServer` ("Only on Charge") still had two hardcoded
numbers — 76000 to start against a measured 16,336-byte startup cost, and 48000 to keep
running against an observed steady state of 43-55 KB. Both deleted and replaced with the
shared policy in `64029f976`; the on-charge server now starts and stays up (8/8 status polls
at 200 over two minutes, where it previously served nothing for minutes).

Still open, and separate: **baseline heap is the real constraint.** ~43 KB free with a 17 KB
largest block at Home is the same story as the reader's 18 silent restarts. Note also that
below ~48 KB free, `CMD:SETTINGS` is refused (its own floor), so a device in that state
cannot be reconfigured over serial without a reboot first — worth knowing before debugging
one remotely.

**Log nit introduced by the deferral fix**: while a background handler defers server start,
`BackgroundWebServer::loop()` re-logs "WiFi already connected, starting server" every tick
(~21 ms). The deferral message itself is throttled to 5 s; that call-site log is not. Only
visible with developer-mode logging on. Not fixed.

## 2026-08-12T22:40Z — The desktop simulator does not build, so the heap gate is not a gate
- **Found by**: claude — while seeding device-measured heap budgets into the simulator (ad hoc)
- **Where**: `src/activities/network/WifiSelectionActivity.cpp:77`, simulator mocks
- **What**: `pio run -e simulator` fails: `'ESP_MAC_WIFI_STA' was not declared in this scope`
  and `'esp_read_mac' was not declared in this scope`. The file is unmodified on this branch
  (last touched by `8963002fd`), so this is pre-existing drift between the firmware sources
  and the simulator's mock layer — the same class as the earlier "test/mock + screen-harness
  break separately from the firmware build" trap.
- **Why it matters more than usual right now**: `f15b9f7de` seeded the simulator's heap model
  with real device numbers (180000 total / 20000 largest, replacing 330000/330000) precisely
  so the simulator can catch fragmentation OOMs before they reach hardware. Those values are
  inert until the simulator compiles. We believe we have a heap gate; we do not.
- **Regression ladder**: this is at least the third occurrence of "a build that is not on the
  routine path silently rots". Rung 3 is a gate — a CI leg or pre-push step that builds
  `-e simulator`, in addition to the existing test and docs.
- **Fixed in `1dc47c824`** via `patch_simulator_hal.py` (esp_mac.h + HalDisplay.h async seam).
  `pio run -e simulator` succeeds and the program runs with the seeded budgets live
  (`MaxAlloc: 20000`, free tracking the 180000 total).
- **The gate is still not a gate**, for two reasons found immediately after:
  1. **Stale SD fixtures.** The simulator's seeded books fail to open:
     `[BMC] Cache version mismatch: expected 9, got 6`. It idles at Home and never enters the
     reader, so it exercises none of the allocation sites that matter (section build, CSS,
     image decode). Regenerate the fixtures, or the heap model measures an empty room.
  2. **Two different notions of "total heap".** The MEM line reports
     `Total: 1048576` (an Arduino-mock constant) while free is computed against sim_heap's
     180000 budget. Cosmetic today, actively misleading the moment someone reads that line
     as the budget.
- **Ladder**: third occurrence of "a build off the routine path silently rots" — rung 3 is a
  gate. The natural one is a CI leg building `-e simulator`.
- **Status**: open (build fixed; coverage and the total-heap inconsistency are not)

## 2026-08-13T00:20Z — The simulator's heap model had three sources of truth; the reader's own gates saw none of them
- **Found by**: claude — closing out the previous entry's two open items
- **Where**: `.pio/libdeps/simulator/simulator/src/esp_system.h:4`, `src/simulator/freertos_compat.h:44`,
  callers at `src/activities/reader/EpubReaderActivity.cpp:1742`, `:1755`, `:2073`
- **What**: three independent "how much heap is there" answers coexisted in the simulator:
  1. `sim_heap.cpp` — the seeded 180000/20000 budget. Correct.
  2. `ESPMock::getHeapSize()` — inline `1024 * 1024`. Produced the self-contradictory
     `Free: 169008, Total: 1048576` line noted in the previous entry.
  3. `esp_get_free_heap_size()` — inline `return 1000000`. **This was the damaging one.**
- **Why (3) mattered**: the reader gates page render (`:1742`) and font prewarm (`:2073`) on
  `esp_get_free_heap_size()` directly, not through `heapguard`. Under the stub both gates saw
  1,000,000 bytes free and passed unconditionally, so the simulator could never enter the
  low-heap paths those gates exist to protect. `[ERS] Heap: before=1000000 after=1000000
  delta=0` was visible in every run and read as instrumentation noise; it was the gate
  announcing it was switched off.
- **Not affected**: `heapguard::freeBytes()`/`largestBlock()` already route through
  `ESP.getFreeHeap()`/`getMaxAllocHeap()` under `SIMULATOR` (`lib/Memory/HeapGuard.cpp:22-36`),
  so every `canAllocate()` guard — including the footnote guard and the background-server
  admission check added earlier today — was budget-backed all along.
- **Fix**: `esp_get_free_heap_size()` and `ESPMock::getHeapSize()` are now declarations, both
  defined in `sim_heap.cpp` against the one budget under the one accounting mutex. Verified:
  `[ERS] Heap: before=162384 after=160328` where it previously read a flat 1000000.
- **Fixture item from the previous entry is resolved**, and was not a firmware bug: the stale
  `epub_*` cache dirs were orphaned by the v6→v9 `book.bin` bump. Home's thumbnail pass calls
  `epub.load(false, true)` — `buildIfMissing=false` — so it deliberately refuses to rebuild a
  full metadata cache for every book on the shelf. Deleting the orphaned dirs lets a real open
  rebuild them; the simulator now reaches the reader and renders pages.
- **Measured once coherent**: peak usage across a Home→Library→Settings→Reader walk is
  ~91.7 KB (min free 88,272 of the 180,000 budget).
- **Status**: fixed

## 2026-08-13T00:25Z — Out-of-scope observations (NOT fixed; do not silently fix)
- **`EpubReaderActivity.cpp:2087` prints a bogus heap delta on the host.**
  `LOG_DBG("ERS", "... delta=%ld", ..., (int32_t)heapAfter - (int32_t)heapBefore)` — `%ld` with
  a 64-bit host `long` prints -2056 as `4294965240`. Correct on device (32-bit `long`),
  wrong in the simulator and in any host build. `%d` would be right on both. One character,
  but it is in the reader and outside the simulator-model change that surfaced it.
- **The absorption plan's premise about `skipLoadingCss` is stale.**
  `plans/` Part C item 1 states `Epub::load(buildIfMissing, skipLoadingCss)` has a parameter
  that "no caller ever passes as true". `src/activities/home/HomeActivity.cpp:487` passes
  exactly that. The plan's "either route it through the new path or delete it" option is
  therefore not available as written — the parameter has a live caller with a real reason.
  Re-derive that item before acting on it.
- **Status**: open (reported, deliberately unfixed)

## 2026-08-13T00:55Z — Evidence-ranked allocation census (input to the pooling work)
- **Found by**: claude — running the harness under the now-coherent heap model
- **How to reproduce**:
  `FORKDRIFT_SIMULATOR_SMOKE_TEST=1 FORKDRIFT_SIMULATOR_SMOKE_PAGE_TURNS=12 <build>/program`
- **Numbers**:
  | measure | value | note |
  |---|---|---|
  | peak single allocation | **23,312 B** | exceeds the 20,000 B largest-block budget |
  | largest block (device, low end) | 17,396 B | measured 2026-08-12 on the X4 |
  | peak usage, test_tables walk | 91,784 B | min free 88,216 of 180,000 |
  | peak usage, demo.epub walk | 143,296 B | min free 36,704 of 180,000 |
- **The headline**: a single 23,312-byte allocation is larger than the largest contiguous run
  the model allows, and larger still than the 17,396 B low end seen on real hardware. Total
  free heap is irrelevant to it — this allocation cannot succeed under fragmentation however
  much is free in aggregate. This is the top pooling target, and it is now a measured fact
  rather than a guess. `sim_heap_biggest_alloc()` reports it; it previously existed only in a
  static destructor that `_exit(0)` guarantees never runs.
- **Guards observed firing** (both real, both on the section-build path):
  - `[PTX] OOM guard: truncating block (low heap, largest=20000)` — three times within a
    single section build. Section build is the dominant consumer.
  - `[CHP] Footnote guard: dropping links (count=0, ...)` — fires at **count=0**, i.e. the
    guard added earlier today rejects the *first* footnote when free heap is within
    `kCriticalFloorBytes` of the requested growth. Correct as written, but it means a book
    silently loses every footnote rather than some. Worth revisiting the floor before this
    ships; it is a behaviour change introduced today.
- **Also visible now**: `css_rules.cache` is opened and missed repeatedly within one reading
  session — the unmeasured release/reload trade from `1b1788ada`, previously invisible.
- **Status**: open — the census is done; the pooling work it ranks has not started

## 2026-08-13T00:58Z — Pre-existing smoke failure on the demo.epub highlight leg (NOT fixed)
- **Where**: `FORKDRIFT_SIMULATOR_SMOKE_BOOK=/books/demo.epub` leg, "Reader back to highlight"
- **What**: `FRAMEHASH changed but was expected identical` — a render of the same state does
  not reproduce its own frame hash. Exit code 2.
- **Not caused by the heap-model change**: verified by re-running with
  `SIM_HEAP_BUDGET=2000000 SIM_HEAP_LARGEST=2000000`, where no heap gate can trip. It fails
  identically. The cause is in the selection/highlight round trip, not the heap.
- **Consequence for the census above**: this leg aborts before the `Sim heap:` line, so the
  demo.epub peak-allocation figure is unavailable until it is fixed. The min-free figure
  (36,704) is still valid, being logged periodically rather than at exit.
- **Status**: open (reported, deliberately unfixed — out of scope)

## 2026-08-13T01:40Z — "Pre-existing" retracted: the FRAMEHASH failure was three real bugs
- **Found by**: claude — after being correctly pulled up for filing this under "pre-existing"
- **Where**: `src/activities/reader/EpubReaderActivity.cpp:2429`, `src/util/AnnotationStore.cpp:74`,
  `src/simulator/SimulatorSmokeTest.cpp` highlight leg
- I had reported the demo.epub `FRAMEHASH changed but was expected identical` failure as
  pre-existing and out of scope. That was wrong twice over: pre-existing says nothing about
  whether a bug is real, and the failure was not one bug but three.

### 1. The selection word index could never be retained on device (FIXED)
`collectSelectableWords` gated retention on `ReaderOptionsMemoryPolicy::canRetainPreview`,
which adds `kReserveLargestBlock` (48000 = one framebuffer) to the request and compares
against `maxAllocHeap`. Retaining a **2,306-byte** word index therefore demanded **50,306
bytes contiguous**. The X4's measured largest block is 17,396-40,948, so the gate could never
pass on real hardware. That policy is correct for its other caller
(`tryCaptureSelectionSnapshotFromFramebuffer`, which really does snapshot a framebuffer) and
wrong here. Consequences, all three from the one constant:
  - the index was always dropped;
  - entering selection then reported `stale-or-unavailable` and **reloaded the page from the
    section file every time** (3 content loads in a 12-second run, each an SD read);
  - the rebuilt word indices no longer matched a persisted highlight, so it re-rendered on
    word 0 — marking text the reader never selected.
**Fixed** by gating on what the code actually allocates via `heapguard::canAllocate`.
**Verified**: `Selection index retained: words=61`, and `SMOKE_SELECTION_CONTENT_LOADS` drops
from 1 to 0 on the normal path.

### 2. `AnnotationStore::add` failed silently four ways (FIXED)
`!loaded`, at-cap, empty text, and oversized text all shared one unlogged `return false`. The
caller shows a generic "failed" popup and stays in selection mode, so an unlogged refusal is
indistinguishable from a rendering bug — which is precisely how this presented, and why the
first diagnosis went to the wrong subsystem. Violates the project's own "ALWAYS log before
error return" rule. **Fixed**: each refusal now logs its reason.

### 3. The smoke test's highlight leg does not do what it says (OPEN)
With logging added, `ANNOTATIONS.add` is never called at all — zero `[ANN]` lines, no
`annotations.bin` written. Frame `067-Annotation-popup-on-highlight.pbm` shows **no popup**:
just the reader with a selection cursor. The Confirm meant to open the actions menu does not
open it, so every later tap in the leg lands somewhere else and `Action::Highlight` is never
reached. The action ordering itself is fine (`buildActions` does put Highlight after
BookNotes), so the fault is in the leg's input/settle assumptions, not the menu.
**The test has therefore never tested highlight persistence**, and its FRAMEHASH assertion was
failing for an unrelated reason. Not fixed — it needs the leg re-derived against the real
popup, which is a separate piece of work from the two firmware bugs above.

### 4. The smoke test is not hermetic (OPEN)
Consecutive runs start at different book positions (`26/31 6%` then `3/44 7%`) because
`progress.bin` persists between runs. Any cross-run frame comparison is meaningless and the
highlight leg exercises a different page each time. Reset reader state at smoke-test start.
- **Status**: 1 and 2 fixed and gated; 3 and 4 open with a concrete next step

## 2026-08-13T20:45Z — On-device flash of today's work: root cause confirmed, fix not yet observed firing
- **Found by**: claude — flashing HEAD (944cc90e1) to the X4 and driving it with device_walk
- **Flash**: both OTA slots (0x10000, 0x650000) + otadata erased. Device boots, navigates, and
  renders pages (`[ERS] Rendered page in 1792ms`, `Progress saved: spine=19 page=0`).
- **The flashed binary genuinely carries today's work** — `strings` finds "Refusing add",
  "Selection index retained", and "SELECTION_INDEX_FALLBACK" in `firmware.bin`. Worth checking
  this way rather than trusting the flash log.

### Root cause confirmed on real hardware
Live readings with developerMode logging on:
```
Free: 80,616   Total: 180,616   Min Free: 29,524   MaxAlloc: 38,900-40,948
```
`MaxAlloc` never exceeds ~41 KB. The old gate
(`ReaderOptionsMemoryPolicy::canRetainPreview`) required `48,000 + retainedBytes` contiguous.
**With 80 KB free it still could not pass**, exactly as diagnosed from the simulator. Total
180,616 also confirms the 180,000 budget seeded into `sim_heap.cpp` was accurate, not a guess.

### What is NOT yet verified
No `Selection index retained: words=N` line was observed on device. The string is in the
binary and `LOG_DBG` is demonstrably working (`[ERS] Page render (tiled)` is also `LOG_DBG` and
appears), so this is not a logging-level problem. `buildSelectionPageIndex` is called from
`renderContents` (`EpubReaderActivity.cpp:2102`) behind `!previewRenderOnly && section`.
Either that call is not reached on the render path taken, or the index build happens on a path
the capture window missed. **The fix is therefore simulator-verified but not yet device-verified
— do not describe it as confirmed on hardware until that line is seen.**

### Separate defect found while driving
One book in My Library shows **"Failed to load EPUB"** and renders blank before settling. This
cannot be caused by the selection-index change, which runs only after a page has loaded. Most
likely the same class as the simulator's stale fixtures (a `.crosspoint` cache built by older
firmware against the current `book.bin` version), but that is a hypothesis, not a diagnosis —
the load failure emitted no `ERR` line at the current log level, which is itself worth fixing.

### Harness note
`device_walk.py`'s `expect` only sees lines arriving *after* the preceding `press` returns, so
`press <BTN> <settle>` followed by `expect` silently misses anything logged during the settle.
Use `press <BTN> 0` and let `expect` do the waiting. This cost a full debugging cycle.
- **Status**: open — device-verification of the selection-index fix, and the EPUB load failure

## 2026-08-13T21:10Z — "Failed to load EPUB" is an intermittent low-heap failure, not a bad book
- **Found by**: claude — driving the X4 over device_walk after flashing HEAD
- **Symptom**: opening a book shows a blank screen that settles to "Failed to load EPUB".
- **It is not the book.** Both books in Recent (`Managing Kubernetes Resources Using Helm`,
  `epub_3657489293`; `Site Reliability Engineering`, `epub_690289643`) open and render
  correctly on demand — `Rendered page in ~1790ms`, `Progress saved`. The failure did not
  reproduce on a clean boot either.
- **It is heap.** The load that failed happened shortly after a flash, while the background
  WiFi server was still up. Measured on the next boot:
  ```
  exit Home: free=52812  min=4676  largest=38900
  ```
  **Min free reached 4,676 bytes** — the lowest ever recorded on this device. Entering the
  reader then tears the background server down in stages, and the serial trace shows exactly
  what it was holding:
  ```
  [WEB] Free heap before stop:        59,480
  [WEB] after server->stop():         63,304
  [WEB] after delete server:          75,660
  ```
  So the background server holds ~16 KB, and a book load attempted while it is resident and
  the heap is already low fails. Once the reader has torn it down, loads succeed. That fully
  explains "fails right after flash/boot, works every time afterwards".
- **Why this matters beyond the symptom**: `min=4676` means the device routinely comes within
  ~4.7 KB of exhaustion during ordinary Home use with the background server on. The
  `kCriticalFloorBytes` of 32 KB that `heapguard` defends is being blown straight through by
  something that is not going through `heapguard`.
- **Silent failure**: the load emitted **no `ERR` line** at all — the user sees "Failed to load
  EPUB" and the serial log says nothing. Same class as the `AnnotationStore::add` defect fixed
  in `ef7d698a0`. The load path needs a logged reason before it returns false.
- **Not fixed here** — this is a real defect with a measured cause, but fixing it means either
  gating book open on available heap or making the reader tear the background server down
  *before* attempting the load rather than after. That is a design decision, not a patch.
- **Status**: open — root-caused and measured, fix deliberately not attempted

## 2026-08-13T21:15Z — Selection index build produces no log on device (still unexplained)
`buildSelectionPageIndex` is called at `EpubReaderActivity.cpp:2102` and the tiled-render log
is at `:2227` — both inside `renderContents` (2064-2297), so the call is on the executed path.
`ENABLE_TEXT_SELECTION` is compiled in (all three strings are present in `firmware.bin` per
`strings`), and `LOG_DBG` works (the tiled-render line is `LOG_DBG` and appears). Yet neither
`Selection index retained` nor `SELECTION_INDEX_FALLBACK` ever appears across many page renders.
Every exit path of `collectSelectableWords` logs, including the `wordCount == 0` case, so
"called and silent" should be impossible. Narrowed but unresolved — the `ef7d698a0` fix stays
**simulator-verified only** until this line is observed on hardware.
- **Status**: open

## 2026-08-13T21:45Z — Background servers now torn down BEFORE the book load (FIXED, device-verified)
- **Where**: `src/activities/reader/EpubReaderActivity.cpp:302` (`onEnter`)
- **The ordering bug**: `EpubReaderActivity::blocksBackgroundServer()` returns true, but it is
  only consulted from `main.cpp`'s loop — which runs *after* `onEnter()` has already loaded and
  laid out the book. `BG_WIFI.stop()` was called synchronously up front, but the on-charge
  `BackgroundWebServer` was left to the reactive path and so came down a tick too late. With no
  heap compaction on this platform, freeing after the allocation has already failed buys
  nothing.
- **Fix**: `BackgroundWebServer::getInstance().stop(true)` alongside the existing
  `BG_WIFI.stop(true)`, before any load. Idempotent (logs "already stopped" and returns).
  Restart is unchanged: once the activity exits, `blocksBackgroundServer()` goes false and the
  existing reconcile in `main.cpp` brings it back. `keepWifi=true` on both — the radio is not
  the expensive part, the server objects and route tables are, and dropping the association
  would cost a reconnect on reader exit.
- **Device-verified** (X4, first open after a clean boot, on-charge server confirmed running):
  ```
  before:  Free: 55,424   [WEB] handleClient active, server running on port 80
  after:   Free: 78,852   ch028.xhtml loaded, Rendered page in 1797ms
  ```
  ~23 KB more headroom at the moment of allocation. This is the condition that previously
  produced "Failed to load EPUB".
- **Not claimed**: the original intermittent failure was never reproduced on demand, so this is
  verified as "the ordering is now correct and the load has ~23 KB more room", not as "the
  intermittent failure is proven gone". `Min Free` still reaches ~4.4 KB during boot/Home,
  which remains open — see the 21:10Z entry.
- **Status**: fixed and device-verified; the underlying Home-time low-water is still open

## 2026-08-13T22:00Z — Clarification: two managers, one server (never both at once)
Language correction for anyone reading the 21:45Z entry. `BackgroundWifiService` (Always mode)
and `BackgroundWebServer` (on-charge/USB mode) each own a `CrossPointWebServer`, and the code
comments in `main.cpp` call them "both servers" — but they are **mutually exclusive at
runtime**, guarded in both directions:
  - `shouldRunOnChargeBackgroundServer()` ends in `&& !input.bgWifiRunning`
  - `evaluateReconcile()` returns `None` when `input.usbBackgroundServerRunning`
So exactly one can be serving. "Two background servers" invites the wrong reading that both
hold heap simultaneously; it is one server with two lifecycle owners, and the ~23 KB measured
in the 21:45Z entry is one server's footprint, not two summed.

This does not change the fix, it sharpens it: exactly one of the two `stop()` calls in
`onEnter` does real work on any given entry. The gap was not "we forgot to free the second
server" but "the mode that was actually running (on-charge) had no synchronous teardown at
all" — `BG_WIFI.stop()` covered Always mode only.

## 2026-08-14T00:30Z — The ~4.4 KB Min Free is HomeActivity's unguarded 48 KB cover buffer (FIXED, device-verified)
- **Found by**: claude — `CMD:HEAPTRACE` on the X4, boot to Home
- **Why it took a day to find**: `ESP.getMinFreeHeap()` is a monotonic since-boot record, so it
  cannot be bisected by watching a running device — you must be present when it drops. And you
  cannot be: the X4's serial is native USB-CDC over USB/IP, every reset drops the host attach,
  and re-attaching takes ~15 s. Measured: after a reset the first line the host ever sees is at
  t=20 s, by which point `Min Free` is already at its floor. The whole causal window is
  unobservable by streaming. `heaptrace` (a3f0fb3c9) exists because of this: it records
  milestones into RAM and dumps them after the host has re-attached.

### What the trace showed
```
boot:autoconnect  free=85960  min=85784  (-47800)   <- WiFi: expensive but SAFE
boot:done         free=83624  min=83560
home:covbuf-pre   free=81884  min=78940
home:covbuf-post  free=33868  min=33868  (-45072)   <- one 48,000-byte malloc
home:covbuf-pre   free=63256  min=12092
home:covbuf-post  free=15240  min=12092             largest=9204
```
- **WiFi is exonerated.** It costs ~48 KB but leaves `min` at 85,784 — well clear of the floor.
- The entire collapse is `HomeActivity::storeCoverBuffer()`, a bare `malloc(48000)` with **no
  heap check of any kind**. It succeeded down to free=15,240 / largest=9,204, i.e. it walked
  straight through `heapguard::kCriticalFloorBytes` (32 KB) — exactly the "path that never
  consults heapguard" that was predicted from the symptom.
- The cover buffer is a pure optimisation: a framebuffer copy so Home navigation can restore the
  rendered cover instead of re-reading cover BMPs from SD. `HeapReclaimRegistry` was already
  allowed to drop it under pressure, so declining it was always a supported outcome.

### Fix
`HomeCoverCachePolicy` (a pure snapshot predicate, same shape as `ReaderOptionsMemoryPolicy`, so
the boundaries are host-testable) with `floorAfter = heapguard::kLowFloorBytes` rather than
`kCriticalFloorBytes`. That is not arbitrary: `HeapGuard.h` defines LOW as the level at which to
"defer optional luxuries (previews, covers, prefetch)", and this buffer is precisely that. The
critical floor would not have been enough — the first of the two allocations above passes a
32 KB-floor check (81,884 - 48,000 = 33,884) and still leaves the following render work to drive
`min` to 12 KB.

### Device-verified result
```
before:  Min Free: 4,424    (worst observed 4,112)
after:   Min Free: 50,524   (49,324 after exercising Home navigation)
```
An ~11x improvement in the margin to exhaustion, and the device now stays above the 32 KB
critical floor for the whole boot-to-Home sequence. With WiFi down there is room and the cache is
still taken, so it is not dead code — it is now conditioned on being affordable.
- **Status**: fixed and device-verified

## 2026-08-14T00:35Z — BaseTheme painted a black placeholder over the cover it had just drawn (FIXED)
- **Found by**: claude — screenshotting the X4 after the cover-cache guard above, which turned a
  latent bug into a visible one
- **Where**: `src/components/themes/BaseTheme.cpp` (`drawBookCard`)
- `coverRendered = coverBufferStored;` conflates two different facts: "the cover bitmap is in the
  framebuffer" and "the 48 KB frame was cached". A later, separate block reads
  `if (!bufferRestored && !coverRendered)` and fills the card — so a cover that was drawn and
  then *not* cached got a solid black `fillRect` painted straight over it, plus the bookmark
  ribbon. That is exactly what the screenshot showed.
- Latent before, because the bare `malloc` essentially always succeeded; the guard made it fire
  on every Home render.
- **Fix**: a local `coverDrawnThisPass` flag, so the placeholder is suppressed when the cover
  actually reached the framebuffer. `coverRendered` keeps its cache-tied meaning deliberately —
  with no cached frame the cover *must* be re-read from SD next pass.
- **Only BaseTheme has this shape.** The other seven themes gate their placeholder on a local
  `hasCover` (did the BMP parse) *inside* the same `if (!coverRendered)` block, so a failed cache
  costs them a redraw, not a black box. `PokemonPartyTheme` documents that fallback explicitly.
  Checked all of them rather than assuming.
- **Not covered by a test**: `drawBookCard` needs a live `GfxRenderer` and is not reachable from
  the host suite. Caught by screenshot, and that is currently the only way to catch it.
- **Status**: fixed; device-verified by screenshot

## 2026-08-14T02:10Z — Reader-open failures were silent in three more places (FIXED)
- **Found by**: claude — auditing the load chain for item 2 of the overnight brief
- `ReaderRegistry::open` had **three** bare `return {}` statements (empty path, entry with no
  factory, factory returned null). `{}` default-constructs `ReaderOpenResult`, whose
  `logMessage` is `nullptr`, and `ActivityManager::goToReader` logged only
  `if (result.logMessage)`. A book that would not open bounced the user back to Home with
  nothing whatsoever on serial. Third occurrence of this exact class today.
- Also silent: `EpubReaderActivity::onEnter`'s `if (!epub) return;`, and `Epub::load`'s
  `!buildIfMissing` branch (expected for cover-thumb generation, but the caller still reports
  "failed to load" and this branch gave no reason).
- `goToReader` now logs **unconditionally**, with path and status, so a future result that
  forgets to set a reason still cannot vanish.
- **Audited, not assumed**: `Section::createSectionFile` already logs every `return false`, and
  SD open failures are logged inside `SDCardManager` with raw `Serial.printf` (so they print at
  any log level, developerMode or not). Neither needed touching. The earlier assumption that the
  whole load path was unlogged was too broad.
- **Test**: `ReaderRegistry::open` takes live `GfxRenderer`/`MappedInputManager` references,
  neither of which the host suite links, so it is not directly callable. Pinned instead by a
  pair in `test_reader_factory_contract.cpp`: a behavioural check that a default-constructed
  `ReaderOpenResult` really does carry a null reason, and a structural check that `open()`
  contains no bare `return {};`. Confirmed the pair discriminates (3 hits pre-fix, 0 post-fix).
- **Status**: fixed; not exercised against a reproduced failure on hardware, because the
  original intermittent failure has never been reproducible on demand

## 2026-08-14T02:20Z — RESOLVED: why "Selection index retained" never logs on device
- **Found by**: claude — device probe on the X4 (temporary log at the call site, since removed)
- The 2026-08-13T21:15Z entry concluded "called and silent should be impossible". The premise
  was wrong in one specific place, and that place is the whole answer.

### Cause 1 — there *was* an unlogged exit (FIXED in 77c4b0354)
`collectSelectableWords` ends in `return !out.empty();`. That is a failure return with no log,
and `buildSelectionPageIndex` turns it into its own silent `return false`. The two compose into
total silence. The claim "every exit path logs, including the `wordCount == 0` case" was exactly
wrong about this one — `wordCount == 0` skips both bounded checks and falls straight through to
the unlogged return.

### Cause 2 — there is genuinely nothing to select
With the exit logged, the device says the same thing on every page of the open book:
```
[PROBE] selguard preview=0 section=1 elements=0
[WRN] [ERS] SELECTION_INDEX_FALLBACK empty counted=0 emitted=0 lines=0
```
The guard passes, the call is made, and **the page has zero elements**. Verified across
ch025–ch030. Screenshot of the reader confirms it: the page renders completely blank.

So the selection index was never "silently failing" — it was correctly reporting that there are
no selectable words, on a book whose pages are empty.

- **Consequence for `ef7d698a0`**: still **not device-verified**. Not because the fix is wrong,
  but because this device's open book has no selectable words at all, so the retained path
  cannot be exercised on it. It needs a book that indexes successfully. Do not upgrade that
  claim until `Selection index retained: words=N` with N>0 is seen.
- **Status**: mystery resolved; `ef7d698a0` verification still blocked on a healthy book

## 2026-08-14T02:25Z — OPEN: sections index to zero elements below a 41 KB free-heap cliff, and the empty result is cached
- **Found by**: claude — while resolving the selection-index mystery above
- **The device is rendering blank pages, and this is the serious finding of the night.**

### The mechanism, exactly
`ParsedText::addWord` ([ParsedText.cpp:269](../lib/Epub/Epub/ParsedText.cpp)) heap-checks before
appending **each word** and discards the word if the check fails:
```cpp
if (!heapguard::canAllocate(kWordGrowthGuardBytes)) {   // 8 KB
  if (!heapTruncated) { heapTruncated = true; LOG_ERR("PTX", "OOM guard: truncating block..."); }
  return;                                                // word dropped
}
```
`heapTruncated` is per-`ParsedText`, so the line logs **once per block**. Hundreds of log lines
means hundreds of *distinct blocks*, not one block logging repeatedly.

The reason it never recovers mid-chapter is arithmetic in `canAllocate`:
```cpp
if (free < bytes || free - bytes < floorAfter) return false;   // floorAfter = 32,768
return bytes <= largestBlock();
```
With `bytes = 8,192` this is false **whenever free heap is under 40,960**, and it returns on that
clause *before* the largest-block test. Measured free during these chapters: **26,048–34,272**.
So every word of every block was discarded for the whole parse.

Zero words per block → no `PageLine` elements emitted → a page with `elements=0`, which is
exactly what the device probe printed (`counted=0 emitted=0 lines=0`).

### Correction to an earlier draft of this entry
An earlier version of this entry blamed **fragmentation** and cited `largest=11252`. That is
wrong. The `largest=` value is only diagnostic text inside the log message; it is not the
predicate that failed. The trigger is **total free heap below ~41 KB**, driven by the JPEG decode
attempts on the same chapters (`[JPG] Not enough heap for JPEG decoder (26048 free, need 36864)`,
repeated for media/file26 through file52, plus `[ERS] OOM: grayscale strip scratch (8000
bytes)`). The device also silently restarted (heap-defrag reboot) at least twice in a few minutes
of paging.

**This matters for the fix**: pooling (item 4 of the overnight brief) addresses large single
allocations against a small largest-block, and would **not** have prevented this. Do not reach
for it here.

### Why it is permanent
**The empty result is sticky**, exactly like the empty-CSS-parse defect of 2026-08-12T19:45Z:
`[SCT] Deserialization succeeded: 1 pages` — the section file was *written* with an empty page.
Re-opening the chapter reads that blank cache and never re-indexes, so the chapter stays blank
even after the heap fully recovers. Transient pressure, permanent damage.

### What a fix needs
- (a) **Refuse to persist a section whose page has zero elements** — treat it as an index failure
  to be retried, not a result to cache. This alone stops the permanent damage and is the cheap
  half.
- (b) Stop holding image-decode memory across the text parse, so the 41 KB cliff is not crossed
  in the first place. That is the real cure and the larger job.
- Also worth reconsidering: an 8 KB speculative reserve on top of a 32 KB floor means text layout
  refuses to add a single word while 40 KB is still free. That is a very expensive guard band for
  a `std::vector` growth step.
- **Status**: open — root-caused and measured, fix deliberately not attempted

## 2026-08-14T14:05Z — The empty-section refusal is device-verified, and it misses the cliff by ~3 KB
- **Found by**: claude — X4, forcing a re-index by changing `fontSize` (which invalidates the
  cached section on a parameter mismatch), then restoring it
- Fix `89c78e78b` verified on hardware end to end:
```
[SCT] Deserialization failed: Parameters do not match
[ERS] Cache not found, building...
[PTX] OOM guard: truncating block (low heap, free=38156 largest=14836)
[SCT] Refusing to cache empty section: 1 block(s) dropped by the heap guard (free=45736 ...)
[ERS] Failed to persist page data to SD
```
  and on the next open it logs `Cache not found, building...` again rather than reading a blank
  cache. **The stickiness is gone** — that was the point of the fix.

### The corrected root cause is now proven, not argued
`free=38156` at the moment the guard tripped, with `largest=14836`. The request is 8,192 bytes,
which fits the largest block four times over. It failed on `free - 8192 < 32768`. This is the
total-free cliff, not fragmentation, and settles the correction made in `814e2d69f`.

### The trade-off is real and now observed
The user sees "Failed to load EPUB" rather than a blank page (screenshot confirms). That is the
honest outcome and it retries, but on *this* device with *this* chapter the heap never clears the
cliff, so the chapter is currently unreadable rather than blank-but-openable. Recorded as the
known cost of the fix, exactly as flagged when it was written.

### The interesting number
`free=37620`–`38156` against a 40,960 threshold — **it misses by roughly 3 KB**. The threshold is
`kWordGrowthGuardBytes` (8 KB) on top of `kCriticalFloorBytes` (32 KB). Reserving 8 KB ahead of a
`std::vector` growth step, on top of a 32 KB floor, is what makes this chapter unindexable; a
smaller guard band, or a lower floor scoped to indexing, would very likely let it through.
**Not changed** — that is a deliberate crash-safety margin (the comment says the guard exists
because `bad_alloc` becomes `terminate()` under `-fno-exceptions`), and re-tuning it is a
judgement call for a human, not a 3am unilateral edit. Flagged for Malcolm.

### Also observed, unexplained
After a failed load the device settled at `Free: 16,772  Min Free: 11,556  MaxAlloc: 9,204` with
the background web server running, and `/api/settings` returned `{"error":"low memory"}`. That is
far worse than the 49–54 KB seen at Home after the cover-cache fix, and the background server
appears to be up while the reader is the current activity — which `blocksBackgroundServer()`
should prevent. Not investigated; may be a separate defect in the reconcile path after a load
failure.
- **Status**: fix verified; guard-band tuning and the post-failure low-heap state both open

## 2026-08-14T16:10Z — Multi-agent sweep: one defect in my own work, one confirmed race, and a pile of unverified leads
- **Found by**: claude, orchestrating cursor-agent (grok-4.6) and agy (Antigravity) as read-only
  reviewers over this repo. Raw reports are in the session scratchpad; **this entry records only
  what I re-verified by reading the code myself.** Agent output is a lead, not a finding.

### VERIFIED and FIXED — `HomeActivity.cpp:1738` claimed "rendered" with no cached frame
An adversarial review of my own `683b62f47` found a second instance of the `c4612a39a` bug in
the classic (non-mediaPicker) Home path: `coverRendered = true` regardless of whether
`storeCoverBuffer()` succeeded, while the SD re-read branch is gated on `!coverRendered` and the
placeholder is that branch's `else if`. Once the cover cache declines, every later pass draws
neither. Fixed in `1ac6ffd18`.
**Worth recording why I missed it**: I read that exact line while writing `c4612a39a` and took it
as evidence of intended semantics, then audited by grepping `coverRendered = coverBufferStored` —
a pattern that can only match the sites that were already correct. Checked `FlowTheme.cpp:197`
and `:248` too; those are safe because FlowTheme keys its re-render on `bufferRestored`.

### VERIFIED, NOT FIXED — `BackgroundWifiService::run()` builds a server it was told to stop
`stopRequested` is checked at `src/network/background/BackgroundWifiService.cpp:152` (inside the
WiFi connect wait) and then **not again until :221** (the service loop). Between those lines the
task runs `onBackgroundNetworkReady()`, a deferral wait loop (:180-182), `new
CrossPointWebServer()` (:185), `server->begin()` (:192), mDNS, and a library shelf refresh.
So a `stop(true)` issued from `EpubReaderActivity::onEnter()` during an in-flight auto-connect
still constructs and starts a full web server — the ~16-23 KB measured in the 2026-08-13T21:45Z
entry — and only tears it down when the loop finally notices. Precisely the wrong moment: the
book load is allocating right then.
This explains the log that looked impossible (background server running while the reader, whose
`blocksBackgroundServer()` returns true, was current). The reconcile path is correct; it simply
cannot reach into a task already past :152.
**Fix is small** — re-check `stopRequested` before `new CrossPointWebServer()` and after the
deferral wait — but it is outside what was approved tonight, so it is filed rather than done.
- **Status**: open, root-caused, fix not attempted

### UNVERIFIED LEADS — plausible, cited, but I have not confirmed them
Do not treat these as findings until someone reads the code. Recording so they are not lost:
- **The `if (!doc) return;` silent trap I fixed for EPUB in `0563fc6d4` still exists in the other
  three readers**: `TxtReaderActivity.cpp:41/:340`, `XtcReaderActivity.cpp:41/:179`,
  `MarkdownReaderActivity.cpp:69/:275`. Also claimed: `TxtReaderActivity.cpp:364` swallows a
  render failure *before* `displayBuffer()`, leaving the previous e-ink page on screen.
- `HomeActivity.cpp:502-504` — a failed `generateThumbBmps` clears `coverBmpPath` with no log;
  user sees a blank cover with nothing on serial.
- `Epub.cpp:215` and `:271` — the result of `readItemContentsToStream` is **ignored**, so a
  missing zip TOC entry yields an empty chapter list while `load()` returns true.
- `CssParser::clear()` (`CssParser.h:104`) only clears elements; bucket arrays and vector
  capacity stay allocated. Only `releaseMemory()` returns them. `Section.cpp:375`
  (`hasFailedLutRecords`) bypasses even the `clear()`.
- Allocation census figures were produced but **look wrong in places** (a claimed 192 KB
  selection snapshot against a ~180 KB heap cannot be a live path as stated). Re-derive before
  using any of it for the pooling work.

### Method note, for whoever runs a sweep next
cursor-agent at 8-wide returned usable output for **1 of 8** workers (the rest exited with empty
files); agy at 2-3 wide returned **8 of 8**. Concurrency, not capability — and a second
cursor-agent session belonging to the human was already running on this machine. Also: agy
silently works in its own scratch copy, which here was pinned at `ef7d698a0` (yesterday's HEAD);
`--add-dir=<repo>` is required or it reviews the wrong code and sounds confident doing it.
Its flags are Go-style and need `--flag=value`, and the prompt must be passed as `--print="..."`.

## 2026-08-14T17:30Z — Guard-band fix device-verified: blank chapters now index, and `Selection index retained` finally observed
- **Found by**: claude — X4, after flashing the `wordgrowth::` guard-band change
- **The guard now asks for what it actually needs.** Live on device:
```
[PTX] OOM guard: truncating block (need=416 free=32696 largest=14324)
```
  **416 bytes**, not the old flat 8,192. It still tripped here, correctly: 416 + 32,768 = 33,184
  against free=32,696, i.e. genuinely at the critical floor rather than 8 KB above it.

### Previously-blank chapters now render
`ch025` and `ch027` of the open book both laid out to zero elements before (see the
2026-08-14T02:25Z entry). After the fix, forcing a re-index (a `fontSize` change invalidates the
cached section on parameter mismatch; restored afterwards, all four render settings verified back
to `fontFamily=0 fontSize=1 lineSpacing=1 screenMargin=5`):
```
ch025: Selection index retained: words=8    -> "Chapter 19 - Load Balancing at the Frontend"
ch027: Selection index retained: words=14   -> renders, 1904ms
```
Screenshot confirms real text where there was a blank page.

### Item 3 of the overnight brief is now fully closed
`Selection index retained: words=N` has **never been observed on hardware** until now — that was
the open question `ef7d698a0` was blocked on, through two sessions. It logs because there is
finally something to select. `ef7d698a0` can be described as device-verified.

### Two limits, both real and both still open
1. **Existing damage is not repaired.** The Section fix prevents *writing* a blank section; it
   does not invalidate ones already on SD. `ch026` still deserialized its old blank cache and
   rendered `counted=0` until forced to rebuild. Clearing it for real needs either a cache wipe
   (recovery menu / delete `.crosspoint/`) or a `SECTION_FILE_VERSION` bump, which would
   re-index every book on every device — a user-visible cost and a deliberate decision, so it is
   not taken here.
2. **Partially truncated sections are still cached.** The run above truncated one block
   (`need=416`) and the resulting section was still written, because the refusal requires *zero*
   elements. That is the documented trade: refusing partial sections would make a chapter this
   device cannot fully index unreadable rather than partly readable.

### Also still open
- The sibling footnote guard (`ChapterHtmlSlimParser.cpp` `kFootnoteGrowthGuardBytes`) keeps the
  flat 8 KB shape and was seen firing in the same trace (`Footnote guard: dropping links
  (count=0, largest=14324)`). A footnote record is larger than a word slot and wants its own
  measurement.
- Image decode is still the thing driving the heap down in the first place:
  `[PNG] Not enough contiguous heap for PNG dimensions (free=29856 largest=14324 need=58912 +
  16384 headroom)` and `[JPG] Not enough heap for JPEG decoder (28788 free, need 36864)`. The
  text now survives that pressure; the images do not.
- **Status**: guard band fixed and device-verified; the four items above remain open

## 2026-08-15T11:20Z — TRMNL renders only on the timed-wake path; the awake path hits two PNG heap walls
- **Found by**: claude — X4 over USB serial, `device_walk.py trmnl-status` and `trmnl-render-test`
- **Where**: `src/activities/boot_sleep/SleepActivity.cpp` (dimensions + decode),
  `lib/Epub/Epub/converters/PngToFramebufferConverter.cpp`

**The fetch side is healthy.** Stored evidence: `fetch_success_count: 13` of 13
`timer_wake_count`, `wifi_success_count: 13`, `last_fetch_ok: true`. The Terminus server
(192.168.86.25:2300) answers correctly and currently serves `refresh_rate: 94` with a PNG
`image_url`. Nothing in the network path is failing.

**The render side is not.** `render_success_count: 8` against 13 completed cycles. The gap is
which path the render ran on:
- *timed-wake* (deep sleep -> minimal boot -> render, nothing else resident):
  `last_render_free_heap: 121404`, `max_alloc: 59380`, completes.
- *awake* (going to sleep from a live UI): measured `free=44028 largest=22516`, fails.

**Wall 1 (FIXED, device-verified, `e2012d0d2`)**: `getDimensions()` instantiated a whole PNG
object (58,912 bytes; guard demands 75,296 CONTIGUOUS) to read the IHDR width/height, which sit
in the first ~33 bytes of the file. `e5d3ad1a9` had already fixed this class for text layout by
restoring `ImageDimsProbe`; SleepActivity was the **second consumer** and was missed — the exact
trap the 2026-07-26T14:10Z entry registers. Now shared as `imagedims::probeFromFile()`.

**Wall 2 (OPEN, = plan 099)**: after the fix the same test advances to `stage: "bw-decode"` and
fails there. **CORRECTION to the first draft of this entry**: the ask is NOT 75,296 contiguous.
`heapguard::canAllocate(bytes, floorAfter)` (`lib/Memory/HeapGuard.h:52`) tests two different
heaps -- (a) `bytes` fits the largest free block, and (b) `floorAfter` bytes of free heap REMAIN
afterwards. The contiguous requirement is therefore 58,912 == `sizeof(PNG)`; the 16,384 is a
total-free floor that already passes. Relaxing the headroom is still useless, but for the
opposite reason to the one first given: it is not the failing condition. (a) is.

**Cheapest available mitigation is server-side, not firmware**: `destPathForMagic`
(`Registration.cpp:107`) already accepts BMP, and BMP needs no decoder object. If Terminus can
be told to serve BMP for this device, wall 2 is bypassed entirely with no firmware change.
Terminus exposes this only through its authenticated web UI — not testable from here.

**Unresolved**: how the 8 timed-wake renders succeeded at all. `max_alloc: 59380` recorded at
those renders is still below the 75,296 the guard demands, so either the recorded figure is
sampled at a different instant than the guard check, or those cycles served BMP. Do not build on
either explanation until it is measured.

- **Also noted, NOT fixed (out of scope)**: `lib/Markdown/MarkdownRenderer.cpp:954` has the same
  `decoder->getDimensions()` pattern and the same exposure. Third consumer of this class.
- **Status**: wall 1 fixed and device-verified; wall 2 open (plan 099); server-side BMP option
  untested and needs the maintainer

## 2026-08-15T12:05Z — NEGATIVE RESULT: reclaiming heap before the sleep render moves the largest block by zero
- **Found by**: claude — X4, prompted by the maintainer asking the obvious question ("how are we
  not just clearing heap before decoding?"). Implemented, measured, reverted.
- **What was tried**: `core::HeapReclaimRegistry::releaseAll()` on the main task at the top of
  `ActivityManager::goToSleep()`, before `replaceActivity()`. The registry already existed with
  two HomeActivity entries and exactly one caller (`BackgroundWifiService::canStartNow()`); the
  image path had never been wired to it.
- **Measured on device, same boot**:
```
idle:                    [MEM] Free: 54264  MaxAlloc: 38900
[REG] heap reclaim: released home cover cache
[REG] heap reclaim: released home carousel frames
[WEB] Free heap before stop: 59868 -> after delete server: 76052
at the decode:           free=89200  largest=38900   need=58912
```
  **~35 KB released — cover cache, carousel frames and the whole web server — and the largest
  contiguous block did not move: 38,900 before, 38,900 after.** Free heap rose to 89,200, so the
  released memory is real; none of it was adjacent to the largest run. The heap is partitioned by
  allocations made early and never released (the 48,000-byte framebuffer and the WiFi/LWIP pools
  are the obvious candidates, not yet confirmed individually).
- **Why this is worth recording rather than retrying**: "free more memory first" is the intuitive
  fix and it is *structurally* unavailable here. Without compaction, freeing helps only when the
  freed block borders the one you need. Reverted rather than shipped: a change with a measured
  zero effect is not worth the diff or the cache-eviction cost.

### RESOLVES the open question from the 2026-08-15T11:20Z entry
Why did 8 timed-wake renders succeed? Because `sizeof(PNG)` is 58,912 and that path's
`max_alloc` was **59,380**. It fits by **468 bytes**. The awake path's 38,900 does not fit at all.
Not a sampling artefact and not BMP — the earlier entry's two guesses were both wrong.

### And it rules out the obvious form of plan 099
`PNGdec.h:159`: `uint8_t ucZLIB[32768 + sizeof(struct inflate_state)];`, commented *"put this
here to avoid needing malloc/free"*. With `ucPixels[15872]` (our `PNG_MAX_BUFFERED_PIXELS`
override) and `ucFileBuf[2048]`, that accounts for the 58,912.
- Heap-allocating `ucZLIB` separately — plan 099's own suggestion — still leaves a **~39,800-byte**
  block, above the 38,900 awake ceiling. It does not clear the wall.
- The 32,768 window is mandated by the PNG format, not a tunable.
- Shrinking `PNG_MAX_BUFFERED_PIXELS` does not touch `ucZLIB`.
So plan 099 as written cannot fix the awake path. Fixing it needs streaming inflate, or reserving
the decoder's block at boot while the heap is still whole, or not using PNGdec.
- **Status**: negative result, reverted, root cause understood; the 468-byte margin on the
  working path is now the headline risk to TRMNL stability

## 2026-08-15T14:10Z — `test/run_host_tests.sh` bootstrap destructively rewrites `platformio.ini`

- **Found by**: claude — during the `GfxRenderer::FrameBufferLoan` port (`swarm/fbloan`)
- **Where**: `test/run_host_tests.sh:27` (`uv run pio pkg install -e default --library
  "bblanchon/ArduinoJson@7.4.2"`), effect lands on `platformio.ini`
- **What**: `pio pkg install --library` persists the dependency by rewriting
  `platformio.ini` through ConfigParser. The rewrite is not a diff — it re-emits the
  whole file. Three things are lost: (a) **every `symlink://open-x4-sdk/...` entry in
  `[base] lib_deps` is dropped**, leaving `lib_deps = bblanchon/ArduinoJson@7.4.2`, so
  the next firmware build fails with `fatal error: EInkDisplay.h: No such file or
  directory`; (b) all comments; (c) values from `platformio.local.ini` are *inlined into
  the committed file*, which puts a developer's personal `build_dir`/`build_cache_dir`
  one `git add` away from history. The bootstrap only runs when `.pio/libdeps` is absent,
  so it hits fresh clones and fresh worktrees — exactly the people least likely to spot
  it in `git status`.
- **Reproduced**: yes, on this worktree. Recovered with `git checkout -- platformio.ini`
  followed by a rebuild, which reinstalled the symlink deps.
- **Why not fixed here**: out of scope (scope was `lib/GfxRenderer/*` plus the loan call
  sites). The fix is small — `pio pkg install` already installs declared dependencies
  from the env, so the `--library` argument is redundant; dropping it, or adding
  `--no-save`, should be enough. Needs verifying against a genuinely empty `.pio`.
- **Status**: open

## 2026-08-15T14:10Z — `open-x4-sdk` submodule pin `8fa0c15d` is not fetchable from its declared remote

- **Found by**: claude — during the `GfxRenderer::FrameBufferLoan` port (`swarm/fbloan`)
- **Where**: `.gitmodules` (url `https://github.com/Unintendedsideeffects/community-sdk.git`),
  gitlink at `open-x4-sdk`
- **What**: `git submodule update --init --recursive` in a fresh worktree fails with
  `remote error: upload-pack: not our ref 8fa0c15d4991e3598a0d6532f249ab42f6191e40`. The
  pinned commit exists only in local checkouts (the sibling `crosspoint-reader/open-x4-sdk`
  has it); it was never pushed to `community-sdk`. Any new clone, any CI runner, and any
  new worktree therefore cannot build at all — the failure surfaces late and confusingly,
  as missing `EInkDisplay.h` / `common/FsApiConstants.h` headers.
- **Workaround used**: `git -C open-x4-sdk fetch <sibling-checkout> 8fa0c15d… && git -C
  open-x4-sdk checkout 8fa0c15d…`.
- **Why not fixed here**: the fix is a push to another repository, which is not mine to
  make, and is outside the scope of this change.
- **Status**: open — needs the maintainer to push `8fa0c15d` to `community-sdk`

## 2026-08-15T14:10Z — a wake-with-restored-frame that hits a chapter build leaves the INDEXING popup on the panel

- **Found by**: claude — during the `GfxRenderer::FrameBufferLoan` port (`swarm/fbloan`)
- **Where**: `src/activities/reader/EpubReaderActivity.cpp:1882`
  (`APP_STATE.consumeTransparentSleepWakePaint()`), popup drawn at
  `src/activities/reader/EpubReaderActivity.cpp:1804`
- **What**: `render()` draws the INDEXING popup and runs `createSectionFile()` when the
  section cache misses, then — further down, before any page paint — returns early if
  `consumeTransparentSleepWakePaint()` is true. That early return exists because the wake
  path restored the previous frame and the panel already shows it, but by then the popup
  has overwritten that frame and been pushed to the panel by `drawPopup()`'s own
  `displayBuffer()` (`src/components/themes/BaseTheme.cpp:710`). The reader is left
  showing "INDEXING" until the next input. Predates this change: the popup already
  destroyed the restored frame before the loan existed. The loan changes only what is left
  in the framebuffer (white rather than the popup), and nothing reads it before the next
  `clearScreen()`.
- **Why not fixed here**: out of scope (scope was `lib/GfxRenderer/*` plus the loan call
  sites), and the fix is a behavioural decision about wake repaint policy, not a
  mechanical one.
- **Status**: open

## 2026-08-15T14:10Z — `ReaderActivity::loadEpub` has no framebuffer loan, unlike upstream

- **Found by**: claude — during the `GfxRenderer::FrameBufferLoan` port (`swarm/fbloan`)
- **Where**: `src/activities/reader/ReaderActivity.cpp:26`, `src/core/registries/ReaderLoader.h:17`
- **What**: Upstream wraps the uncached container/OPF/spine/TOC parse in a
  `FrameBufferLoan` (`upstream/master:src/activities/reader/ReaderActivity.cpp:63`). Our
  `loadEpub` delegates to the shared `core::loadDocumentNoThrow<T>` template, which has no
  `GfxRenderer`, draws no popup, and cannot see whether `book.bin` already exists — and
  `ReaderActivity::onEnter()` runs *without* the `RenderLock`
  (`src/activities/ActivityManager.cpp:194` unlocks before calling it), so a loan there
  could race `renderTaskLoop()`. Porting it means plumbing a renderer through a loader
  shared with Xtc/Txt, adding a popup, and taking a lock in a lifecycle hook. The chapter
  build (the larger of the two peaks) does get the loan.
- **Why not fixed here**: brief scoped this to "the minimum call sites needed to make the
  loan real"; this one is a loader restructure with a concurrency hazard attached.
- **Status**: open — deliberate gap, worth a follow-up once a `buildscratch` consumer exists

## 2026-08-15T16:05Z — Loan mechanism absorbed and merged; the Terminus path is not yet wired to it
- **Found by**: claude, integrating `swarm/fbloan` + `swarm/miniz` (cursor swarm, 2 workers)
- **Merged at**: `00477d111`. Gates: host **436 cases / 10829 assertions**, `-e default`
  SUCCESS (Flash 91.6%, +6,184 bytes), `-e simulator` SUCCESS. Flashed to the X4.

**No regression**: baseline heap after the merge is `Free 53,864 / MaxAlloc 38,900`,
identical to the pre-swarm measurement.

**What now exists**: `lib/Memory/BuildScratch` (registry), `GfxRenderer::FrameBufferLoan`
(lender), `lib/miniz` `InflateStream` (consumer), and `PngToBmpConverter` rewired onto it
with the ring allocated *before* the scanline buffers.

**What still does not work**: the Terminus repack is unchanged on device --
```
[TRMNL] Repack skipped: no 32768-byte block (free=65364 largest=26612)
```
The loan is taken around a **chapter build** in `EpubReaderActivity`, which is not a
moment the Terminus fetch ever passes through. The mechanism is present but nothing on
this path claims from it.

**Why the fetch is the wrong place to claim, even now.** The lent bytes ARE the displayed
image (`EINK_DISPLAY_SINGLE_BUFFER_MODE=1`). A Terminus refresh runs in the background
while the user may be looking at Home, so claiming there would white out a live screen.
The loan is only legal where the screen is about to be fully redrawn anyway.

**The remaining step**, therefore: move the repack to the **sleep-render** path, which is
both the moment the framebuffer is legitimately expendable and the moment the image is
actually needed. Two routes worth measuring against each other:
1. Take a loan around the repack there and let `InflateStream` claim its ~43 KB.
2. `InflateStream::init(false)` one-shot, where the destination holds the whole output and
   no window is allocated at all -- for an 800x480 1-bit image that destination is 48,000
   bytes, i.e. the framebuffer again.
Route 2 is cheaper if the decode can be driven in one forward pass; route 1 is more
general. Neither is measured yet.

- **Also note**: the `heapguard::canAllocate(32768)` precheck in
  `src/features/terminus_sleep/Registration.cpp` is now sized against uzlib's window and
  will need re-deriving once the path moves to `InflateStream`.
- **Status**: mechanism landed and device-verified as non-regressive; TRMNL still renders
  only on the timed-wake path, by the same 468-byte margin as before

## 2026-08-15T17:30Z — The sleep-path framebuffer loan is safe only because SleepActivity never renders asynchronously
- **Found by**: claude — checking the loan added in `63505837f` before running it on device
- **Where**: `src/activities/boot_sleep/SleepActivity.cpp` (`repackPngForSleep`),
  `src/activities/ActivityManager.cpp:194`

A `FrameBufferLoan` nulls `GfxRenderer::frameBuffer`. Anything that draws while one is
held dereferences null. The two loans in this tree are protected differently, and only one
of them is protected structurally:

- **`EpubReaderActivity`** (from `swarm/fbloan`): the loan sits inside `render()`, which
  `renderTaskLoop()` (`ActivityManager.cpp:83`) calls **while holding `RenderLock`**. The
  render task therefore cannot re-enter and draw. Safe by construction.
- **`SleepActivity`** (this one): `ActivityManager` does `lock.unlock()` at `:194`
  *before* calling `onEnter()`, so this loan is held with **no `RenderLock`**. It is safe
  only because `SleepActivity` overrides no `render()` and never calls `requestUpdate()` —
  it draws entirely within `onEnter()`, so it never notifies the render task at all.

**That is a load-bearing invariant that the code does not state anywhere.** Giving
`SleepActivity` a `render()` override, or any `requestUpdate()` call, would introduce a
null-framebuffer race that is timing-dependent and would not reproduce reliably.

- **Device evidence**: the production `goToSleep()` path was exercised on an X4 with the
  loan active (`exit Home` -> `enter Sleep` -> repack -> `Inflate scratch claim ok: 41136
  bytes` -> landscape render). No fault. The race is real but did not fire, which is
  exactly why it deserves a written invariant rather than reliance on testing.
- **Options if this is ever tightened**: take a `RenderLock` inside `repackPngForSleep`
  (safe today — no sleep path holds it — but it would deadlock the moment one did), or
  have `buildscratch` refuse to lend while the render task has work queued.
- **Status**: open (documentation-only; no defect observed). Second occurrence of the
  "large-block borrow vs. concurrent drawing" class, so per the regression ladder this
  entry is the rung-2 documentation. A third occurrence should get an automatic gate.

## 2026-08-21T12:15Z — Blanket `delay(50)` before every image extraction is now redundant with `imagedims::probeFromFile`'s own bounded retry

- **Found by**: claude — during the PNG-correctness brief (worktree `cpr-png-claude`),
  scope was `lib/PngToBmpConverter/` + the image-decode path in `lib/Epub/converters/`
- **Where**: `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:950` (the `delay(50); //
  Give SD card time to sync` right after `cachedImageFile.close()`), which precedes a
  call to `probeImageDimensions()` at line 956, which calls
  `imagedims::probeFromFile()` (`lib/Epub/Epub/converters/ImageDimsProbe.cpp`)
- **What**: This brief added bounded retry-after-failure to
  `imagedims::probeFromFile()` (3 attempts, 50ms apart, only on a failed open/read) to
  absorb transient SD-sync latency right after a cache-file write. That retry now
  covers exactly the case this `delay(50)` was defending against, but the blanket delay
  was left in place. The result: every single image in every chapter pays a flat 50ms
  penalty on the common (already-synced) path, in addition to the new bounded retry on
  the rare (not-yet-synced) path — the two mechanisms are solving the same problem
  twice, and the cheaper one (retry-only-on-failure) makes the more expensive one
  (blanket delay) dead weight.
- **Why not fixed here**: `ChapterHtmlSlimParser.cpp` is in `lib/Epub/Epub/parsers/`,
  outside this task's scope (`lib/PngToBmpConverter/` and the image-decode path in
  `lib/Epub/Epub/converters/`). Removing it also risks interacting with other
  in-flight parallel work on the same file in sibling worktrees.
- **Status**: open
## 2026-08-21T12:14Z — Brief's "1024-byte serialized word cap" claim does not match this worktree's `TextBlock.cpp`
- **Found by**: claude, `absorb/epubparse-claude` — porting `3319aa172` (long-word/CJK
  continuation) per `plans/BRIEF-epubparse.md`
- **Where**: `lib/Epub/Epub/blocks/TextBlock.cpp`, `lib/Serialization/Serialization.h:179-215`

`plans/BRIEF-epubparse.md` states the priority defect as: "`lib/Epub/Epub/blocks/
TextBlock.cpp` caps a serialized word at 1024 bytes on read, and layout can emit longer
... The result is a section that serializes but will not deserialize." Neither half of
that claim holds against this worktree's actual files:

- `TextBlock::deserialize()` (`TextBlock.cpp:151-210`) has no per-word byte cap at all. It
  reads each word via `serialization::readString()`, whose only limit is the *generic*
  65536-byte sanity check shared by every string field in the codebase
  (`Serialization.h:182,195,207`), not a TextBlock- or word-specific one.
- `plans/2026-08-21-crosspoint-crossink-absorption-handoff.md:317-320` separately
  describes a *different* number — `MAX_SERIALIZED_WORD_BYTES = 200` — attributed to
  `TextBlock.cpp:12-13`. That constant, and the `readBoundedWord` it names, do not exist
  in this worktree's `TextBlock.cpp` either. That finding is describing another agent's
  patch state, not what is on disk here.
- Separately, `ChapterHtmlSlimParser::characterData()`'s `partWordBuffer[MAX_WORD_SIZE +
  1]` (`ChapterHtmlSlimParser.h:22,43`, `MAX_WORD_SIZE = 200`) already forced every
  `ParsedText::addWord()` call to carry ≤200 UTF-8 bytes, on both sides of my change — the
  buffer physically cannot hold more before its overflow branch fires
  (`ChapterHtmlSlimParser.cpp:1740`). So a >1024-byte (or >200-byte) single serialized
  word was not reproducible through this parser before my change either.

**What the real, present-tense defect was** (and what my change fixes): the overflow
branch flushed the 200-byte chunk as a normal, independent word — `continues=false` — so
a long URL or unbroken CJK run longer than 200 bytes became N separately-breakable,
space-joined tokens instead of one continuous run (`ChapterHtmlSlimParser.cpp:1740-1763`,
now sets `nextWordContinues = true` on the carried-over remainder). That is a real
rendering/line-breaking defect — matches the upstream commit's stated intent — just not
the cache-corruption mechanism the brief describes. See the accompanying SUMMARY for the
regression test (`test/host/test_parsed_text_word_continuation.cpp`) and the resulting
max single-fragment word length (unchanged: 200 bytes, both before and after).

- **Status**: reported, not fixed further — brief's STOP list does not cover "the
  described defect mechanism doesn't reproduce"; the continuation-flag fix stands on its
  own merits and is in scope regardless. Worth confirming with whoever wrote the brief
  whether they were describing a different (upstream or WIP) tree.

---

## 2026-08-21T15:40Z — Callers of `WifiCredentialStore::addCredential()` ignore its return, so the new 64-byte bound becomes silent data loss

- **Found by**: cursor (adversarial review of `absorb/creds-claude`), harvested by claude at integration
- **Where**: `src/features/web_wifi_setup/Registration.cpp:87`; `src/main.cpp:1022-1023`;
  `src/activities/network/WifiSelectionActivity.cpp:582-591`
- **What**: `addCredential()` gained a 64-byte password bound and can now fail where it
  previously could only fail on the `MAX_NETWORKS` cap. These three call sites discard
  the return value. `Registration.cpp:87` then saves and replies `200 WiFi credentials
  saved` with an empty store. Two of them set `lastConnectedSsid` *before* calling it, so
  a rejected password leaves `lastConnectedSsid` naming a network that has no credential.
  Generalisable: adding a bound to a function whose callers ignore its return converts
  input validation into data loss.
- **Why not fixed here**: outside the credential-store slice's owned scope; each call site
  needs its own error path and user-facing message, and two are in activities.
- **Status**: open

## 2026-08-21T15:41Z — `removeCredential() && addCredential()` short-circuits after a destructive first step

- **Found by**: cursor (adversarial review of `absorb/creds-claude`), harvested by claude
- **Where**: `src/network/server/CrossPointWebServer.cpp:1153-1154`
- **What**: renaming a network runs `WIFI_STORE.removeCredential(oldSsid) &&
  WIFI_STORE.addCredential(ssid, password)`. With the new 64-byte bound this has a live
  trigger it did not have before: a pre-upgrade entry whose stored password exceeds 64
  bytes gets removed and persisted, then the add is rejected, then a `400` is returned.
  The network is gone from RAM and from disk.
- **Why not fixed here**: the fix is to validate before destroying (or restore on
  failure), in the web server rather than the store.
- **Status**: open

## 2026-08-21T15:42Z — `ObfuscationUtils` decodes before bounding, so a corrupt `wifi.json` allocates first

- **Found by**: cursor (adversarial review of `absorb/creds-claude`), harvested by claude
- **Where**: `ObfuscationUtils` base64 decode, reached from `wifi_credentials::parse`
- **What**: a corrupt `wifi.json` carrying a multi-kilobyte `password_obf` and no
  `password_len` is decoded in full before any bound applies -- on a 380KB no-PSRAM
  device. Upstream `c507e5447` added a bounded overload (`maxDecodedLength` + `tooLong`).
  Entries that *do* carry `password_len` are now rejected before decoding, so only
  integrity-field-less legacy entries are exposed.
- **Why not fixed here**: `ObfuscationUtils` is outside the slice's scope and shared with
  other callers.
- **Status**: open

## 2026-08-21T15:43Z — `removeCredential()` leaves `lastConnectedSsid` dangling

- **Found by**: cursor (adversarial review of `absorb/creds-claude`), harvested by claude
- **Where**: `src/util/WifiCredentialStore.cpp` (`removeCredential`)
- **What**: removing the network that is currently `lastConnectedSsid` does not clear it,
  so every caller must null-check a name that resolves to nothing. Upstream fixes this.
- **Why not fixed here**: a behaviour change beyond the five ported invariants; needs its
  own test plus a look at the auto-connect state machine.
- **Status**: open

## 2026-08-21T15:44Z — A Lua script reading the network list can trigger an SD write

- **Found by**: cursor (adversarial review of `absorb/creds-claude`), harvested by claude
- **Where**: `src/util/LuaManager.cpp:496`
- **What**: calls `WIFI_STORE.loadFromFile()`, which can now perform a migration rewrite
  as a side effect. A script that only reads the network list can therefore cause an SD
  write. Related: `CrossPointWebServer.cpp:1082-1100` and `LuaManager.cpp:497,532` still
  hand-build the password-free view instead of using `getCredentialSummaries()`, which
  now returns exactly that under the lock -- migrating them would stop plaintext
  passwords being copied into the web and Lua paths at all.
- **Status**: open

## 2026-08-21T15:45Z — OPDS feed body has no total-size cap; expat allocates before any field bound applies

- **Found by**: cursor (adversarial review of `absorb/opds-claude`), harvested by claude
- **Where**: `lib/OpdsParser/OpdsParser.cpp` (the per-field `assignBounded`/`appendBounded`)
- **What**: the bounds were applied at the wrong layer. expat grows its own internal
  attribute-value buffer *before* any callback fires, so on a 380KB no-PSRAM device a
  hostile feed exhausts the heap inside the parser while every bound we added sits
  downstream of the allocation it was meant to prevent. Needs a cap where the body is
  read and fed to the parser, with a clean abort once hit.
- **Why not fixed here**: the remediation agent hit its usage limit after fixing the other
  three findings in that review.
- **Status**: open -- highest-severity item remaining from the 2026-08-21 absorption wave
