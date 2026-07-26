# Development Workflow

This page defines the expected local workflow before opening a pull request.

## 1) Fork and Create a Focused Branch

- Fork the repository to your own GitHub account
- Clone your fork locally and add the upstream repository if needed
- Enable repo hooks once per clone from `crosspoint-reader/`: `git config core.hooksPath scripts/hooks` (from the ForkDrift monorepo root, use `crosspoint-reader/scripts/hooks`)

Example: `git checkout -b feature/anki-sync-integration`

## 2) Implement with Scope in Mind

- Confirm your idea is in project scope: [SCOPE.md](../../SCOPE.md)
- Prefer incremental changes over broad refactors.
- Adhere to the project's C++20 standards and naming conventions.

## 3) Commit Messages

We use semantic commit messages to keep the history clean and readable. Start your commit message with one of the following prefixes:

- `feat:` (new feature)
- `fix:` (bug fix)
- `docs:` (documentation changes)
- `refactor:` (code refactoring)
- `test:` (adding or updating tests)
- `chore:` (maintenance tasks)

Example: `feat: add support for custom user fonts`

## 4) Git Hooks

The project includes git hooks in `scripts/hooks/` to automate checks. You should install them to ensure your changes meet project standards:

```sh
git config core.hooksPath scripts/hooks
```

- **`pre-commit`**:
  - Regenerates staged generated assets when needed.
  - Automatically formats staged C/C++ files with `clang-format`.
  - Guards against manual edits to `I18nStrings.cpp` (which is generated).
  - Generates the **full** profile and runs `uv run pio run -e custom`.
- **`pre-push`**:
  - Runs `uv run pio check` (static analysis).
  - Builds the same **full** profile (`pio run -e custom`).
  - Reuses the pre-commit build cache when `HEAD^{tree}` matches (so commit-then-push is usually cppcheck-only).

### Priming the build cache: `scripts/prime-precommit.sh`

The pre-commit hook caches build results keyed by the staged-tree OID (computed by
`git write-tree` **after** clang-format and generated-header mutations). A manual
`pio run` never writes this cache file, so honouring the hook without priming means
paying a redundant ~4-minute rebuild on top of whatever you just built by hand.
That is the real reason `git commit --no-verify` became the local fast path — and
why three compile regressions reached the repository in a single day (see
`docs/FINDINGS.md` for the `std::span`, `BackgroundWifiService`, and
`ENABLE_BOOKMARKS=0` entries).

**`--no-verify` is no longer the fast path.** Use the priming script instead:

```sh
# Stage your changes first, then:
./scripts/prime-precommit.sh        # prime what is currently staged
./scripts/prime-precommit.sh -a     # git add -A first, then prime (mirrors git commit -a)
```

After a successful prime, `git commit` (without `--no-verify`) hits the cache and
returns in **seconds**.

A few important properties:

- **The cache is keyed by the staged tree, not by time.** Priming is only valid for
  the exact content staged at the time. Restaging or editing any file after priming
  invalidates the cache for that new tree — the next commit will build again, which
  is intentional.
- **Failures are cached too.** If the build fails, the cache records the failure so
  a subsequent `git commit` is blocked instantly without rebuilding. Fix the error,
  restage, and prime again.
- **The priming script IS the hook.** It invokes `scripts/hooks/pre-commit` directly,
  so the mutations (formatting, generated headers) and the cache key are identical by
  construction. There is no risk of priming a tree the hook would reject.

## 5) Manual Local Checks

If you don't use the hooks, run these manually before pushing:

```sh
uv run ./bin/clang-format-fix
uv run pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
bash test/run_host_tests.sh
python3 scripts/validate_contract_server.py
uv run python scripts/generate_build_config.py --profile full
uv run pio run -e custom
```

## 6) Open the PR

- Target the **`fork-drift`** branch.
- Use a semantic title matching your commit format (e.g., `fix: avoid crash on malformed epub`).
- Fill out the PR template completely.
- Include reproduction and verification steps for bug fixes.

For community expectations, see [GOVERNANCE.md](../../GOVERNANCE.md).
