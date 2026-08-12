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
