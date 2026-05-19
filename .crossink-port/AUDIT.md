# CrossInk port subagent audit

Generated: 2026-05-19 (HEAD `02b6e04b`, branch `feature/absorb-crossink`)

Sources: git history, working tree, stashes, `PORT-TRACKER.md`, `WORKTREES.md`,
`SUBAGENT.md`, `COMMIT-QUEUE.md`, `port-worktree.sh`, `.last-build`, agent markers.

Missing docs: `SPRINT-PLAN.md` (not present).

---

## 1. Commits reality

### `port(crossink):` commits vs tracker `[x]`

| Port commit | CrossInk hash | Tracker line | Match |
|-------------|---------------|--------------|-------|
| `d8853c6d` add Minimal theme | `91f11a03` | `[x] 91f11a03` | OK — on origin |
| `0cc74978` roundedraff padding | `684946c5` | `[x] 684946c5` | OK — on origin |
| `2e5c47a0` resize menu items | `8ab81889` | `[x] 8ab81889` | OK — on origin |
| `cc925e7c` remove base theme book overlay | `004b893e` | `[x] 004b893e` | OK — **local only** (unpushed) |
| `02b6e04b` home progress bar | `642402f8` | `[x] 642402f8` | OK — **local only** (unpushed) |
| `c5260f30` deduplicated port-build coordinator | — | (infra, not tracked) | OK — **local only** (unpushed) |

### Subsumed (not `[x]`, intentional)

| CrossInk hash | Tracker | Subsumed by |
|---------------|---------|-------------|
| `c0a56228` minimal theme category font | `[≈]` | `d8853c6d` Minimal tip port |

### False positives / negatives

| Type | Count | Detail |
|------|-------|--------|
| False `[x]` (marked done, no git commit) | **0** | All five `[x]` items have matching `port(crossink):` commits on `feature/absorb-crossink`. |
| False negative (commit landed, tracker still `[ ]`) | **0** | Every feature `port(crossink):` commit maps to a tracker `[x]`. |
| Incomplete port (commit landed, follow-up WIP) | **1** | `004b893e` overlay removed in `cc925e7c`, but `STR_CONTINUE_READING` i18n cleanup is **WIP uncommitted** (working tree + `stash@{1}`). |

**Score:** 5/81 feature commits landed (6.2%). Tracker `[x]` accuracy is currently clean; prior false `[x]` problem described in `SUBAGENT.md` appears resolved for the five home/theme items.

**Push gap:** Local is **3 commits ahead** of `origin/feature/absorb-crossink` (`c5260f30`, `cc925e7c`, `02b6e04b`). Remote tip: `2e5c47a0`.

---

## 2. Subagent outcomes (by lane)

| Lane | CrossInk targets (examples) | Status | Evidence |
|------|----------------------------|--------|----------|
| **home** | `004b893e`, `642402f8`, `8ab81889`, `684946c5`, `91f11a03`, grid `ea9568c3` | **LANDED** (5/7 Phase-1 independents) | Commits `d8853c6d`…`02b6e04b`. Grid + carousel cluster still `[ ]`. |
| **reader / controls** | `07bb45b4`, `41209464`, `c924a36d` | **WIP UNCOMMITTED** | `EpubReaderActivity.cpp` hunks in `stash@{0}`, `stash@{9}` only. No `port(crossink):` commit. |
| **file** | `e803b890`, `f17569ad`, `a27ee056`, etc. | **WIP UNCOMMITTED** | `FileBrowserActivity.*` in `stash@{0}`, `stash@{5}`, `stash@{9}`. No commit. |
| **web** | `3cc346a1`, `071da53f` | **WIP UNCOMMITTED** | `FilesPage.html` shift-key styling in `stash@{0}`, `stash@{2}`. Generated headers stale in stashes. No commit. |
| **renderer** | `97c39de7`, font/emoji cluster | **LOST / NOT STARTED** | No stash, no commit, no worktree. |
| **settings** | Phase 4 power-button / picker cluster | **NOT STARTED** | No stash, no commit. |
| **infra** | build coordinator, SUBAGENT, worktrees | **PARTIAL** | `c5260f30` landed (unpushed). `SUBAGENT.md`, `WORKTREES.md`, `port-worktree.sh`, cursor rule **staged, not committed**. `COMMIT-QUEUE.md` **untracked**. **Zero lane worktrees created.** |

### Agent / sprint markers

| Marker | Present? |
|--------|----------|
| `.crossink-port/.cleanup-done` | No |
| `.crossink-port/.sprint-ready` | No |
| `SPRINT-PLAN.md` | No |
| `/home/malcolm/Code/ForkDrift/worktrees/` | **Missing** (directory does not exist) |
| `port/*` branches (local or remote) | **None** |
| `.crossink-port/.build.lock` | Yes (stale risk — multiple `pio` processes observed) |
| `.crossink-port/.last-build` | `status=fail`, `head=02b6e04b`, `ram_percent=` empty |

### Git worktrees (actual)

```
/home/malcolm/Code/ForkDrift/crosspoint-reader   02b6e04b [feature/absorb-crossink]
/tmp/crosspoint-core-api-isolation-assess.*      (prunable, detached)
/tmp/forkdrift-dry-merge                         (prunable)
/tmp/forkdrift-pokedex-verify-*                  (prunable)
/tmp/forkdrift-push-*                            (prunable)
```

No `crosspoint-reader-<lane>` worktrees exist.

### Stash inventory (10 port-related; indices drift vs `COMMIT-QUEUE.md`)

| Stash | Label | Lane content | Disposition |
|-------|-------|--------------|-------------|
| `stash@{0}` | port-wip-all-sources | file + web + reader + i18n yaml | **Recover** → split into lane worktrees |
| `stash@{1}` | port-wip-i18n-overlay-followup | i18n keys/strings (`STR_CONTINUE_READING`) | **Recover** → home/i18n follow-up to `004b893e` |
| `stash@{2}` | web-port | FilesPage.html + stale infra dupes | **Partial recover** (HTML only; ignore infra dupes in HEAD) |
| `stash@{3}` | wip-before-controls-port | mixed home themes + file hdr + i18n | **Drop** home parts (in HEAD); extract file hdr if needed |
| `stash@{4}` | all-port-wip | overlay + progress themes | **Drop** — superseded by `cc925e7c`, `02b6e04b` |
| `stash@{5}` | all-wip-before-web-port | file browser + settings html | **Recover** file parts only |
| `stash@{6}` | wip-non-web | partial progress bar | **Drop** — in `02b6e04b` |
| `stash@{7,8}` | wip-unrelated | i18n churn | **Drop** or cherry-pick if keys still needed |
| `stash@{9}` | wip-mixed-port-changes | file + reader + theme leftovers | **Recover** file/reader hunks only |

**Note:** `COMMIT-QUEUE.md` stash index mapping is **stale** (e.g. it calls `stash@{0}` web-port; actual `stash@{0}` is `port-wip-all-sources`).

---

## 3. Blockers (why slow)

1. **Single-tree parallel agents** — All port work ran on one checkout. Hot files (`BaseTheme.cpp`, `LyraTheme.cpp`, `HomeActivity.cpp`, `I18nStrings.cpp`, `FileBrowserActivity.cpp`) were edited concurrently → overwrites, 10 overlapping stashes, mixed WIP.

2. **Worktree workflow never instantiated** — `port-worktree.sh` exists (staged) but `/home/malcolm/Code/ForkDrift/worktrees/` was never created. No `port/home`, `port/file`, etc. branches. Integration merge loop in `WORKTREES.md` cannot run.

3. **Concurrent builds / lock contention** — Multiple `uv run pio run` / `port-build.sh` processes observed at audit time. `.build.lock` present; `.last-build` records `status=fail` with truncated log (build likely interrupted, not a compile error in log tail).

4. **Uncommitted infra blocks clean handoff** — SUBAGENT/worktree docs staged but not on branch; subagents may not see committed workflow rules.

5. **No `port/*` push → no integration merges** — Lane branches were never pushed; parent cannot merge sequentially per `WORKTREES.md`.

6. **Dirty working tree** — Unstaged i18n edits (`I18nKeys.h`, `I18nStrings.cpp`) block clean builds/commits; yaml keys for `STR_CONTINUE_READING` still present in 22 locale files.

7. **Pre-commit / generated-file churn** — Stashes contain massive `*Html.generated.h` diffs; applying whole stashes blindly will reintroduce conflicts.

8. **Deferred dependencies** — Lyra carousel cluster correctly blocked on Phase 3 stats; not a process failure but limits apparent home-lane progress.

---

## 4. Recommended actions (ordered)

1. **Kill stray builds** — Ensure no concurrent `pio`/`port-build.sh`; remove stale `.crossink-port/.build.lock` if no build running; rerun `./.crossink-port/port-build.sh` once to refresh `.last-build`.

2. **Commit staged infra** (separate commit from this audit): `SUBAGENT.md`, `WORKTREES.md`, `port-worktree.sh`, tracker/coordinator updates, cursor rule — so subagents read committed workflow.

3. **Push the 3 unpushed port commits** — `git push origin feature/absorb-crossink`.

4. **Reset working tree i18n** — `git restore lib/I18n/` or commit overlay follow-up (`STR_CONTINUE_READING` removal) as its own `port(crossink):` commit after green build.

5. **Create lane worktrees** — `./.crossink-port/port-worktree.sh create file` then `web`, `reader`, `settings` as needed.

6. **Recover stashes into lanes** (not integration tree): file ← `stash@{0}`/`stash@{9}` hunks; web ← `FilesPage.html` from `stash@{0}`/`stash@{2}`; i18n follow-up ← `stash@{1}`. Drop superseded stashes `{4,6}` after verification.

7. **Lane commit → push → integration merge** — One `port/<lane>` branch at a time, merge to `feature/absorb-crossink`, build, then flip `[x]`.

8. **Update `COMMIT-QUEUE.md`** — Fix stash index drift; add “do not redo” list below.

---

## 5. Block stale parallel agents

Future subagents must **NOT** redo:

| Already in git (do not re-port) | CrossInk hash |
|---------------------------------|---------------|
| Minimal theme | `91f11a03` |
| RoundedRaff padding | `684946c5` |
| Menu item resize | `8ab81889` |
| Base theme book overlay removal | `004b893e` |
| Home progress bar | `642402f8` |
| Minimal category font tweak | `c0a56228` (subsumed) |
| Port-build coordinator | (infra `c5260f30`) |

| WIP exists in stash — extract, don't rewrite from scratch | Target hash |
|-------------------------------------------------------------|-------------|
| Hidden files toggle | `e803b890` |
| Gray disabled shift key (URL entry) | `3cc346a1` |
| i18n `STR_CONTINUE_READING` cleanup | follow-up to `004b893e` |

| Explicitly deferred — do not start until prerequisites | Reason |
|----------------------------------------------------------|--------|
| Lyra carousel cluster (`971a62ee`, `8154c9a5`, `9a9054cb`, `bdc65f27`, `0c7e11a3`) | Needs Phase 3 reading stats + carousel plumbing |
| Recent books grid (`ea9568c3`, `a45de2df`) | After file lane merged |

| Lost / safe to assign fresh | Notes |
|-----------------------------|-------|
| Renderer lane (all Phase 2) | No WIP anywhere |
| Settings / power-button cluster (Phase 4) | No WIP |
| `f17569ad` delete book cache | No stash |

---

## Summary counts

| Metric | Value |
|--------|-------|
| Feature ports **landed** | **5** / 81 (6.2%) |
| Infra commits landed | 1 (`c5260f30`, unpushed) |
| Tracker `[x]` items | 5 (all verified) |
| False `[x]` | **0** |
| Lanes **blocked** or WIP-only | **4** (reader, file, web, settings-not-started) + infra partial |
| Lane worktrees created | **0** |
| Unpushed local commits | **3** |
| Port-related stashes | **10** (most superseded or mixed) |
