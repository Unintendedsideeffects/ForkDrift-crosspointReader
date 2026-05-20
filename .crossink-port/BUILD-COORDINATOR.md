# CrossInk port build coordinator

> **Subagents:** read `.crossink-port/SUBAGENT.md` before any edit.

Entry point: `./.crossink-port/port-build.sh` (symlink: `scripts/port-build.sh`).

Never run `uv run pio run` directly during port work. One coordinator run per tree fingerprint.

**Parallel agents:** create an isolated lane worktree first — see `WORKTREES.md` and
`./.crossink-port/port-worktree.sh create <lane>`. Never share the integration
checkout across concurrent port agents.

See script for fingerprinting, `flock` lock (`.build.lock`), cache (`.last-build`), and exit codes:
0 ok/cached, 1 build fail, 2 lock timeout, 3 setup error.

**Pre-commit OFF for port sprint:** `.crossink-port/PRE_COMMIT_OFF` skips the hook’s `pio run`.
Port agents must still run `./.crossink-port/port-build.sh` before commit. Remove the file to
restore the hook after the sprint.
