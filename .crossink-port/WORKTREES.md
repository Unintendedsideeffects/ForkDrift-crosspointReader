# CrossInk parallel port — git worktree workflow

> **Subagents:** read `.crossink-port/SUBAGENT.md` before any edit.

Parallel port agents MUST NOT share one working tree. A single checkout causes
overwrites in hot files (`BaseTheme.cpp`, `LyraTheme.cpp`, `HomeActivity.cpp`,
`I18nStrings.cpp`) and races on `./.crossink-port/port-build.sh` / `pio`.

## Layout

| Role | Path | Branch |
|------|------|--------|
| Integration (parent, docs only) | `/home/malcolm/Code/ForkDrift/crosspoint-reader` | `feature/absorb-crossink` |
| Integration (merges) | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-integration` | `feature/absorb-crossink` |
| Lane worktree | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-<lane>` | `port/<lane>` |

Lanes: `home`, `reader`, `file`, `web`, `settings`, `renderer`, `integration` — one
feature area per lane (`integration` = merge-only, no `port/integration` branch).

## Quick start

```bash
./.crossink-port/port-worktree.sh create home
cd /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-home
# implement one PORT-TRACKER item
./.crossink-port/port-build.sh
git add -A && git commit -m "port(crossink): <subject> [<crossink-hash>]"
git push -u origin port/home
```

## Per-lane loop

1. Create worktree: `port-worktree.sh create <lane>`
2. Edit only files for that lane's scope (see lane map below)
3. Build: `./.crossink-port/port-build.sh` (lock is per worktree directory)
4. Commit one logical port per commit
5. Push branch `port/<lane>`
6. Stop — do not merge from integration agent in the lane worktree

## Integration (integration worktree or parent)

Use `port-worktree.sh create integration` or the parent repo (docs-only on parent).
Do **not** port features on the integration checkout while lane agents are active.

1. `git fetch origin`
2. Merge lane branches **sequentially** (one merge, one build):
   ```bash
   git merge --no-ff origin/port/home -m "merge port/home: <summary>"
   ./.crossink-port/port-build.sh
   ```
3. Update `PORT-TRACKER.md` `[x]` only after the commit is on `feature/absorb-crossink`
4. Repeat for `port/reader`, `port/file`, `port/settings`, `port/renderer`, `port/web`

Prefer merge order: `home` → `reader` → `file` → `settings` → `renderer` → `web` (web
touches generated HTML headers last).

## Lane scope map

| Lane | Typical paths / features |
|------|--------------------------|
| `home` | `HomeActivity.*`, theme `drawRecentBookCover`, carousel, progress bar, grid view |
| `reader` | `EpubReaderActivity.*`, reader controls, page overlay, status bar |
| `file` | `FileBrowserActivity.*`, hidden files, mark finished, cache delete |
| `web` | `src/network/html/*`, WiFi/OPDS pages, shift-key URL UX |
| `settings` | settings activities, power-button mapping, control pickers, i18n for settings |
| `renderer` | font loading, text/emoji rendering, underline/strikethrough, line metrics |
| `integration` | merge `port/<lane>` → `feature/absorb-crossink`, tracker `[x]`, push integration |

## Rules

- **Never** run two `pio` builds in the same worktree concurrently
- **Never** start a second parallel agent without `port-worktree.sh create <lane>`
- Lane branches fork from current `feature/absorb-crossink` at create time; rebase
  lane onto integration before merge if integration moved forward
- Remove stale worktrees: `port-worktree.sh remove <lane>`

## Helper commands

```bash
./.crossink-port/port-worktree.sh list
./.crossink-port/port-worktree.sh remove home
```
