# Commit queue — integration worktree (`feature/absorb-crossink`)

Generated: 2026-05-19. Parent repo at `e972763e`.

## Already landed locally (push pending)

Merge/push these four commits to `origin/feature/absorb-crossink` in order:

| # | Commit | Subject | Notes |
|---|--------|---------|-------|
| 1 | `c5260f30` | port(crossink): add deduplicated port-build coordinator | Infra; deduped after reflog reset from duplicate `62c200d7` |
| 2 | `cc925e7c` | port(crossink): remove base theme book overlay [004b893e] | `BaseTheme.cpp` only |
| 3 | `02b6e04b` | port(crossink): home progress bar [642402f8] | Themes + `HomeActivity`; depends on #2 signature |
| 4 | `e972763e` | port(crossink): temporary pre-commit bypass for port sprint | `PRE_COMMIT_OFF` + SUBAGENT.md; unblocks doc-only commits |

```bash
git push origin feature/absorb-crossink
```

## Uncommitted (stashed before infra commit)

| Stash | Contents |
|-------|----------|
| `port-wip-file-browser-active` | File browser hidden-files WIP + i18n + PORT-TRACKER edits |
| `port-wip-all-sources` | File browser, web shift-key, reader tweak (older snapshot) |
| `port-wip-i18n-overlay-followup` | Remove `STR_CONTINUE_READING` after overlay port |

Apply only in lane worktrees — never on integration tree with parallel agents running.

## Stash inventory — recover in this order

Do **not** apply whole stashes blindly; they overlap and duplicate work now in HEAD.

| Priority | Stash | Theme | Action |
|----------|-------|-------|--------|
| — | `stash@{2}` all-port-wip | overlay + progress | **Drop** — superseded by commits #2–#3 |
| — | `stash@{1}` wip-before-controls-port | mixed home + i18n + file hdr | **Drop** after verifying — duplicate of #2–#3 + noise |
| — | `stash@{4}` wip-non-web | partial progress bar | **Drop** — in `02b6e04b` |
| — | `stash@{5,6}` wip-unrelated | i18n churn only | **Drop** or cherry-pick keys if still needed |
| 1 | `stash@{3}` all-wip-before-web-port | **file browser**: hidden files toggle, long-press HOME | Port in `port/file` worktree → `e803b890` |
| 2 | `stash@{7}` wip-mixed-port-changes | **file browser** + partial **home** theme | Extract file-browser hunks only; home parts duplicate HEAD |
| 3 | `stash@{0}` web-port | **web**: FilesPage shift-key gray styling + stale port-build copy | Port in `port/web` worktree → `3cc346a1`; ignore BUILD-COORDINATOR dupes (in HEAD) |
| 4 | (none yet) | **controls** power-button / side-button | Not in stash — fresh port in `port/settings` worktree |
| 5 | (none yet) | **reader** status bar / overlay | Fresh port in `port/reader` worktree |

### Stash drop commands (after verification)

```bash
git stash drop stash@{2}
git stash drop stash@{1}
git stash drop stash@{4}
git stash drop stash@{5}
git stash drop stash@{6}
```

Keep `stash@{0}`, `stash@{3}`, `stash@{7}` until lane worktrees absorb them.

## Recommended next port order (new work, via worktrees)

1. **Push** existing 3 commits
2. **`port/file`**: hidden files toggle (`e803b890`) from stash@{3}
3. **`port/file`**: mark finished action (`a27ee056`) — no stash yet
4. **`port/web`**: gray disabled shift key (`3cc346a1`) from stash@{0}
5. **`port/settings`**: short power = Confirm cluster (Phase 4 commits)
6. **`port/home`**: recent books grid (`ea9568c3`, `a45de2df`) — after file lane merged

## Forensic note

Parallel subagents on one tree caused: duplicate coordinator commit + reset, mixed
overlay/progress WIP in `BaseTheme.cpp`/`LyraTheme.cpp`, 8 overlapping stashes,
and `pio`/pre-commit races. Use `port-worktree.sh create <lane>` before any new
parallel port agent.
