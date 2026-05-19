# CrossInk port restart manifest (parent spawns agents)

**Integration HEAD:** `022e284e` on `feature/absorb-crossink`  
**Stop rule:** If `.crossink-port/STOP_SPRINT` exists, read it and **exit immediately** (auditor left it set; parent removes when restarting sprint).  
**Build:** `./.crossink-port/port-build.sh` only. **Pre-commit:** skipped while `PRE_COMMIT_OFF` exists.

Spawn **integration first** (alone). Then up to **4** lane agents in parallel.

---

## Agent 0 — integration (run first, sequential)

```
READ .crossink-port/SUBAGENT.md FIRST. Use: ./.crossink-port/port-worktree.sh create integration
Do not edit main tree. Exit when done. Check STOP_SPRINT and exit if present.

You are the integration agent on feature/absorb-crossink.

1. If STOP_SPRINT exists, exit.
2. Parent repo (this tree): docs-only OK. Push integration:
   git push origin feature/absorb-crossink
3. Remove stale lane worktrees (behind HEAD):
   ./.crossink-port/port-worktree.sh remove file
   ./.crossink-port/port-worktree.sh remove home
   ./.crossink-port/port-worktree.sh remove reader
   ./.crossink-port/port-worktree.sh remove web
4. Recreate integration worktree at current HEAD.
5. Apply preserved work → lane branches (do NOT redo committed hashes):
   - stash@{0} 2de26a15 (reader+i18n) → port/reader worktree, commit reader hunks only
   - stash@{1} 883328f5 (BaseTheme disabled key) → port/web
   - stash@{5} web-port FilesPage.html → port/web → 3cc346a1
   - stash@{2,4} i18n STR_CONTINUE_READING cleanup → port/home or small integration commit
   Drop stash@{3,6,7,8} after verifying superseded by de4df016/02b6e04b/cc925e7c.
   Patches in .crossink-port/preserved-patches/ are backup only.
6. Merge port/<lane> branches one at a time (home → reader → file → web → settings → renderer),
   port-build.sh after each merge. Update PORT-TRACKER [x] only after merge on feature/absorb-crossink.
7. rm .crossink-port/STOP_SPRINT when sprint ready to resume (parent decision).
```

---

## Agent 1 — home (`port/home`)

| Field | Value |
|-------|--------|
| Branch | `port/home` |
| First hashes | `ea9568c3`, `a45de2df` |
| Source | Fresh port (after file lane merged per tracker note); optional `stash@{2,4}` for i18n overlay cleanup only |

```
READ .crossink-port/SUBAGENT.md FIRST. Use: ./.crossink-port/port-worktree.sh create home
Do not edit main tree. Exit when done. Check STOP_SPRINT and exit if present.

Lane: home. Worktree: /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-home
Branch: port/home (from current feature/absorb-crossink).

Port ONE item per commit:
1. ea9568c3 — toggleable recent books grid view (HomeActivity + themes)
2. a45de2df — progress in grid title (depends on #1)

BLOCKED (already on integration): 91f11a03, 684946c5, 8ab81889, 004b893e, 642402f8.

Optional: apply stash@{2} or stash@{4} ONLY for STR_CONTINUE_READING / I18n cleanup tied to 004b893e.

./.crossink-port/port-build.sh before each commit.
git push -u origin port/home when lane batch done.
```

---

## Agent 2 — reader (`port/reader`)

| Field | Value |
|-------|--------|
| Branch | `port/reader` |
| First hashes | `07bb45b4`, `41209464`, `97c39de7` |
| Source | `stash@{0}` `2de26a15`, `stash@{3}` reader hunks only |

```
READ .crossink-port/SUBAGENT.md FIRST. Use: ./.crossink-port/port-worktree.sh create reader
Do not edit main tree. Exit when done. Check STOP_SPRINT and exit if present.

Lane: reader. Worktree: /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-reader

1. git stash show -p stash@{0} — apply EpubReaderActivity / menu hunks only (skip i18n mass-regen unless needed for your commit).
2. Port 07bb45b4 — status bar padding (adapt to ForkDrift status bar layout).
3. Port 41209464 — bookmark menu + scrollbar >6 items.
4. 97c39de7 — verify [≈] in PORT-TRACKER; do NOT redo if WIDE metrics already match.

BLOCKED: 49c123f9 (landed on integration as 507e185a).

./.crossink-port/port-build.sh per commit. push port/reader.
```

---

## Agent 3 — file (`port/file`)

| Field | Value |
|-------|--------|
| Branch | `port/file` |
| First hashes | `f17569ad`, `a27ee056` |
| Source | Fresh port (e803b890 + 49c123f9 already on integration) |

```
READ .crossink-port/SUBAGENT.md FIRST. Use: ./.crossink-port/port-worktree.sh create file
Do not edit main tree. Exit when done. Check STOP_SPRINT and exit if present.

Lane: file. Worktree: /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-file

BLOCKED (on integration): e803b890 (de4df016), 49c123f9 (507e185a).

Port next:
1. f17569ad — delete book cache from file browser (tracker may say skipped — re-evaluate scope)
2. a27ee056 — mark finished action (Phase 3; may need stats stubs)

Do NOT re-apply stash file-browser hunks for hidden files / long-press.

./.crossink-port/port-build.sh per commit. push port/file.
```

---

## Agent 4 — web (`port/web`)

| Field | Value |
|-------|--------|
| Branch | `port/web` |
| First hashes | `3cc346a1`, `071da53f` |
| Source | `stash@{1}`, `stash@{5}`, `preserved-patches/wip-lane-web-baseTheme-disabled-key-20260519.patch` |

```
READ .crossink-port/SUBAGENT.md FIRST. Use: ./.crossink-port/port-worktree.sh create web
Do not edit main tree. Exit when done. Check STOP_SPRINT and exit if present.

Lane: web. Worktree: /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-web

If worktree create fails on open-x4-sdk symlink: fix submodule path in parent, then recreate worktree.

1. Apply stash@{1} or patch — BaseTheme disabled shift key gray (3cc346a1) — may belong in renderer/home; prefer FilesPage + keyboard UX in web lane if split.
2. Apply stash@{5} FilesPage.html shift-key styling (3cc346a1).
3. Port 071da53f — web UI style tweak.

Regenerate HTML headers via project script after HTML edits (not hand-edit *.generated.h).

./.crossink-port/port-build.sh per commit. push port/web.
```

---

## Parallelism summary

| Order | Agent | Max parallel |
|-------|-------|----------------|
| 1 | integration | 1 (alone) |
| 2 | home, reader, file, web | up to 4 after integration push + worktree recreate |

**Do not** assign two agents the same lane worktree.

## Block list (all agents)

`91f11a03`, `684946c5`, `8ab81889`, `004b893e`, `642402f8`, `e803b890`, `49c123f9`, full `97c39de7` re-port.
