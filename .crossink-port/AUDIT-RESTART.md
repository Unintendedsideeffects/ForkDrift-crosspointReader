# CrossInk port sprint — audit restart (2026-05-19)

Branch: `feature/absorb-crossink` @ `022e284e` (integration tree)  
Auditor action: preserve WIP, stop sprint agents, document kill/restart rules.

`SPRINT-PLAN.md` is not present; fast-lane order follows `PORT-TRACKER.md` Phase 1 independents + Phase 4/5 items below.

---

## 1. Git snapshot (at audit end)

| Check | Result |
|-------|--------|
| `git status` | Clean tracked tree; untracked: `STOP_SPRINT`, `preserved-patches/`, build logs |
| Ahead of `origin/feature/absorb-crossink` | **10 commits** (see §2) |
| `git stash` | **12** port-related entries on `feature/absorb-crossink` (`stash@{0}`…`stash@{11}`) + fork-drift stashes below |
| Worktrees | `crosspoint-reader` (integration), `crosspoint-reader-{file,home,reader,web}` — **stale** at `e972763e`/`02b6e04b` vs integration `022e284e` |
| `STOP_SPRINT` | **Created** (empty sentinel) |
| `PRE_COMMIT_OFF` | **Present** (committed in `e972763e`) |
| `.sprint-ready` / `.cleanup-done` | **Absent** |

### `port(crossink):` commits on integration (newest first)

```
022e284e document git worktree parallel port workflow
a4e14ba1 mandate git worktrees for parallel port subagents
d42c183f update tracker for file browser lane
507e185a file browser delete on hold not release [49c123f9]
de4df016 toggle hidden files in file browser [e803b890]
f9220493 audit subagent port progress and blockers
e972763e temporary pre-commit bypass for port sprint
02b6e04b home progress bar for current book [642402f8]
cc925e7c remove base theme book overlay [004b893e]
c5260f30 add deduplicated port-build coordinator
2e5c47a0 resize menu items to fit homescreen [8ab81889]  (on origin)
0cc74978 roundedraff padding [684946c5]                  (on origin)
d8853c6d add Minimal theme [91f11a03]                    (on origin)
```

**Landed feature ports:** 8 CrossInk hashes with commits on integration (7 on origin + 3 local-only until push, plus 2 file-browser commits landed during audit on main tree — **do not re-port**).

---

## 2. Preservable work inventory

### A. Committed on `feature/absorb-crossink` (safe — push, do not redo)

| CrossInk hash | Commit | Notes |
|---------------|--------|-------|
| `91f11a03` | `d8853c6d` | Minimal theme |
| `684946c5` | `0cc74978` | RoundedRaff padding |
| `8ab81889` | `2e5c47a0` | Menu resize |
| `004b893e` | `cc925e7c` | Base theme overlay removal |
| `642402f8` | `02b6e04b` | Home progress bar |
| `e803b890` | `de4df016` | Hidden files toggle |
| `49c123f9` | `507e185a` | Long-press delete on hold |
| `c0a56228` | — | Subsumed by Minimal port `[≈]` |
| `97c39de7` | — | Subsumed `[≈]` — ForkDrift WIDE already in range |

### B. Uncommitted → stashed / patched during audit

| Lane | Location | Content | Preserve as |
|------|----------|---------|-------------|
| **reader** | `stash@{0}` `2de26a15` | `EpubReaderActivity.cpp`, `EpubReaderMenuActivity.h`, i18n regen, tracker tweak | Apply in `port/reader` worktree |
| **web** | `stash@{1}` `883328f5` | `BaseTheme.cpp` disabled-key gray fill → `3cc346a1` | Apply in `port/web` |
| **home/i18n** | `stash@{2}` `c086479d` | `STR_CONTINUE_READING` removal, tracker | Apply in `port/home` or integration cleanup commit |
| **mixed** | `stash@{3}` `7ee35e0b` | file + web HTML + reader (pre-`de4df016`) | **Cherry-pick hunks only** — file parts superseded |
| **home/i18n** | `stash@{4}` `cfe677f2` | overlay follow-up i18n | Merge with `stash@{2}` |
| **web** | `stash@{5}` `3d9b85a5` | `FilesPage.html` shift-key | `port/web` |
| **file** | `port/file` worktree dirty | Duplicate hidden-files WIP | **Drop** after worktree reset to `022e284e` (`de4df016` on integration) |
| **reader** | `port/reader` worktree | `EpubReaderActivity.cpp` partial | Prefer `stash@{0}` after worktree recreate |

### C. Patch files (`.crossink-port/preserved-patches/`)

| File | ~size | Use |
|------|-------|-----|
| `wip-main-dirty-20260519.patch` | 123K | Reader + i18n snapshot (also in `stash@{0}`) |
| `wip-lane-file-worktree-20260519.patch` | 73K | **Stale** — superseded by `de4df016`; archive only |
| `wip-lane-web-baseTheme-disabled-key-20260519.patch` | 1K | Web lane `3cc346a1` |
| `wip-main-integration-20260519.patch` | 1K | Early audit snapshot — superseded |

### D. Lost / unrecoverable

- Agent transcripts only (no code): partial renderer/settings attempts, aborted parallel main-tree ports before `STOP_SPRINT`.
- `port/web` worktree: `open-x4-sdk` symlink error — **remove and recreate** worktree; do not resume in place.

---

## 3. Kill decisions

| Target | Action | Reason |
|--------|--------|--------|
| Subagents polling `.sprint-ready` / `.cleanup-done` | **KILL** — `touch .crossink-port/STOP_SPRINT` | Sprint phase over; markers never existed |
| Stale `pio` / `platformio` for `crosspoint-reader` + `worktrees/crosspoint-reader-*` | **KILL** (`pkill` scoped to repo paths) | Orphan parallel builds; `.build.lock` removed |
| Parallel port agents on **main integration tree** | **KILL** (do not resume) | Commits `de4df016`/`507e185a` landed on main during sprint — violates worktree rule; future work **worktrees only** |
| Redo `642402f8`, `004b893e`, `91f11a03`, `8ab81889`, `684946c5`, `e803b890`, `49c123f9` | **BLOCK** | Already committed |
| Redo `97c39de7` full port | **BLOCK** | Marked `[≈]` subsumed in tracker |
| Stale lane worktrees at `e972763e` / `02b6e04b` | **RESET** via `port-worktree.sh remove` + `create` after integration push | Behind integration `022e284e` |
| `stash@{3,4,6,7,8}` superseded home/theme stashes | **DROP** after lane agents verify | Duplicate HEAD commits |
| Lane workers (fresh spawn) | **RESTART** | See `RESTART-MANIFEST.md` |

---

## 4. Preservation actions taken

1. `git stash push` → `port-audit-20260519-lane-reader-i18n` (`stash@{0}`)
2. `git stash push` → `port-audit-20260519-lane-web-baseTheme` (`stash@{1}`)
3. Exported patches under `.crossink-port/preserved-patches/`
4. `touch .crossink-port/STOP_SPRINT`
5. Killed scoped `pio` processes; removed stale `.build.lock` files

**Not done:** `git push` (integration agent), stash drops (after lane verification).

---

## 5. Tracker / queue hygiene

- `PORT-TRACKER.md`: **8** `[x]` items match git (`91f11a03` … `49c123f9`, `e803b890`).
- `COMMIT-QUEUE.md` / `AUDIT.md`: stash index **stale** — use this file + `stash list` after audit stashes.
- False `[x]` for `97c39de7` was avoided (now `[≈]`).

---

## 6. Next step (parent agent)

1. Spawn **integration** agent first (sequential) — push 10 commits, reset worktrees, apply audit stashes → lane branches.
2. Spawn up to **4** lane agents in parallel — prompts in `RESTART-MANIFEST.md`.
3. Each agent: read `SUBAGENT.md`, `port-worktree.sh create <lane>`, `port-build.sh`, exit on `STOP_SPRINT` (remove sentinel when sprint restarts).

Do **not** spawn subagents from this auditor run.
