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
