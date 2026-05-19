# READ THIS FIRST — mandatory for every CrossInk port subagent

You are porting CrossInk commits into ForkDrift (`crosspoint-reader`). **One lane = one git
worktree.** Never implement ports on the integration checkout while another agent ports.

## Six rules (non-negotiable)

1. **One lane = one git worktree.** Do not run parallel port implementation on the main
   checkout `feature/absorb-crossink`. Create a lane worktree first:
   ```bash
   ./.crossink-port/port-worktree.sh create <lane>
   ```
   Lanes: `home`, `reader`, `file`, `web`, `settings`, `renderer`, `integration`.
   Worktrees: `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-<lane>`.
   Branch per lane: `port/<lane>` from `feature/absorb-crossink` (integration lane uses
   `feature/absorb-crossink` directly — see `WORKTREES.md`).

2. **Build only via `./.crossink-port/port-build.sh`** inside **that** worktree. Never run
   raw `pio run` / `uv run pio run`. The build lock is per worktree (`.build.lock`).

3. **One commit per feature:** `port(crossink): <subject> [<crossink-hash>]`. Push
   `port/<lane>` when the lane item is done.

4. **Merges only in the integration worktree** (or parent repo on `feature/absorb-crossink`):
   merge `port/<lane>` branches **one at a time**, run `port-build.sh` after each merge.
   Lane worktrees must not merge into integration themselves.

5. **Do not mark `PORT-TRACKER.md` `[x]`** until the commit exists on `feature/absorb-crossink`.
   If work is only on `port/<lane>`, note `[~]` or “pending merge” in your handoff.

6. **Why:** Parallel agents on one tree overwrote WIP, broke pre-commit, and marked `[x]`
   falsely — only 3/82 ports actually landed. Worktrees isolate files, builds, and commits.

## Quick start (lane agent)

```bash
cd /home/malcolm/Code/ForkDrift/crosspoint-reader
./.crossink-port/port-worktree.sh create reader
cd /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-reader
# pick one PORT-TRACKER item for this lane — see WORKTREES.md scope map
./.crossink-port/port-build.sh
git add -A
git commit -m "port(crossink): <subject> [<hash>]"
git push -u origin port/reader
```

Report back: lane, commit SHA, tracker lines touched (do not flip `[x]` unless merged).

## Quick start (integration agent)

```bash
./.crossink-port/port-worktree.sh create integration
cd /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-integration
git fetch origin
git merge --no-ff origin/port/home -m "merge port/home: <summary>"
./.crossink-port/port-build.sh
# update PORT-TRACKER [x] only after merge + green build
git push origin feature/absorb-crossink
```

Merge order: `home` → `reader` → `file` → `settings` → `renderer` → `web` (see `WORKTREES.md`).

## Anti-patterns

- **DO NOT** edit `feature/absorb-crossink` in the parent repo while another subagent ports
  in a lane worktree (docs-only commits on parent are OK).
- **DO NOT** run two port subagents in the same worktree.
- **DO NOT** run `pio` / PlatformIO directly during port work.
- **DO NOT** merge lane branches from inside a lane worktree.
- **DO NOT** check `[x]` in `PORT-TRACKER.md` before the commit is on `feature/absorb-crossink`.


## Pre-commit (port sprint)

Pre-commit normally runs `uv run pio run` and blocks parallel port agents. **OFF for the
crossink port sprint** while `.crossink-port/PRE_COMMIT_OFF` exists (committed on
`feature/absorb-crossink` during the sprint).

- **Disable:** `touch .crossink-port/PRE_COMMIT_OFF` (already present during sprint)
- **Re-enable:** `rm .crossink-port/PRE_COMMIT_OFF` and commit the removal after the sprint
- **Still required:** run `./.crossink-port/port-build.sh` before every commit manually

Hooks live in `scripts/hooks/pre-commit` (`git config core.hooksPath scripts/hooks`). If your
clone only has `.git/hooks/pre-commit`, run:
`git config core.hooksPath scripts/hooks`

## Reference docs

| File | Purpose |
|------|---------|
| `WORKTREES.md` | Lane paths, scope map, merge order |
| `BUILD-COORDINATOR.md` | Build coordinator behavior |
| `PORT-TRACKER.md` | Per-commit checklist |
| `port-worktree.sh` | Create / list / remove worktrees |
| `port-build.sh` | Locked, fingerprinted builds |
