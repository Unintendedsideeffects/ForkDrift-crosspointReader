# CrossInk port — lane merge protocol

Integration branch: `feature/absorb-crossink`  
Integration checkout: `/home/malcolm/Code/ForkDrift/crosspoint-reader` (docs + merges only)

## Lane completion

When a lane item is done in its worktree:

```bash
cd /home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-<lane>
./.crossink-port/port-build.sh
git add -A
git commit -m "port(crossink): <subject> [<crossink-hash>]"
git push -u origin port/<lane>
```

## Integration merge (one lane at a time)

Run on the **main** integration checkout (or `crosspoint-reader-integration` worktree).
Do **not** merge from inside a lane worktree.

```bash
cd /home/malcolm/Code/ForkDrift/crosspoint-reader
git fetch origin
git checkout feature/absorb-crossink
git merge --no-ff port/<lane> -m "merge(port): <lane> lane"
./.crossink-port/port-build.sh
```

Update `PORT-TRACKER.md` `[x]` only after the merge commit is on `feature/absorb-crossink`
and the post-merge build passes.

## Merge order

Merge sequentially; rebuild after **each** merge:

1. `home`
2. `reader`
3. `file`
4. `settings`
5. `renderer`
6. `web`

Prefer `web` last — it touches generated HTML headers.

## Push integration

After each successful merge (or batch when sprint is unblocked):

```bash
git push origin feature/absorb-crossink
```

## Stop rule

If `.crossink-port/STOP_SPRINT` exists, **do not merge** until the parent removes it.
Lane worktrees may still commit and push `port/<lane>` branches.

## Worktree paths

| Lane | Path | Branch |
|------|------|--------|
| home | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-home` | `port/home` |
| reader | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-reader` | `port/reader` |
| file | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-file` | `port/file` |
| web | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-web` | `port/web` |
| settings | `/home/malcolm/Code/ForkDrift/worktrees/crosspoint-reader-settings` | `port/settings` |

Helper: `./.crossink-port/port-worktree.sh list`
