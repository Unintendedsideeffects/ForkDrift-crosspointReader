---
name: update-repo
description: Use when updating the crosspoint-reader fork from upstream, especially when the open-x4-sdk submodule also needs to be merged, pushed to the fork, and repointed in the parent repo. Covers safe fork-drift update flow, submodule remote expectations, verification steps, and CI-sensitive checks before push.
---

# Update Repo

Use this skill for repository maintenance on `crosspoint-reader`, especially when:

- syncing `fork-drift` with `upstream/master`
- updating the `open-x4-sdk` submodule
- repairing CI after submodule drift
- preparing a clean "update fork from upstream" change

## Required repo assumptions

- Parent repo remotes:
  - `origin` = `git@github.com:Unintendedsideeffects/crosspoint-reader.git`
  - `upstream` = `https://github.com/crosspoint-reader/crosspoint-reader.git`
- Submodule `open-x4-sdk` remotes:
  - `origin` = `https://github.com/Unintendedsideeffects/community-sdk.git`
  - `upstream` = `https://github.com/open-x4-epaper/community-sdk.git`
- `.gitmodules` should point `open-x4-sdk` at the fork URL, not upstream, so GitHub Actions can fetch fork-only submodule SHAs.

Check first:

```bash
git remote -v
git -C open-x4-sdk remote -v
cat .gitmodules
git submodule status
```

## Standard update flow

### 1. Update parent repo from upstream

```bash
git fetch upstream
git checkout fork-drift
git merge upstream/master
```

If conflicts appear, resolve them in the parent repo before touching the submodule pointer.

### 2. Update the submodule from upstream

```bash
git -C open-x4-sdk fetch upstream
git -C open-x4-sdk checkout fork-drift-sdcard-readfile
git -C open-x4-sdk merge upstream/main
```

If the submodule needs a different maintenance branch, keep using a branch on the fork remote. Do not move the parent repo to a submodule SHA that exists only locally.

### 3. Publish the submodule branch before updating the parent pointer

```bash
git -C open-x4-sdk push origin fork-drift-sdcard-readfile
git -C open-x4-sdk rev-parse HEAD
git -C open-x4-sdk ls-remote --heads origin fork-drift-sdcard-readfile
```

The parent repo must never reference a submodule SHA that is missing from the fork remote. If CI says `not our ref`, this is the first thing to check.

### 4. Record the new submodule pointer in the parent repo

```bash
git add open-x4-sdk .gitmodules
git status --short
git commit -m "chore: update fork-drift and open-x4-sdk"
```

## CI-sensitive verification

Run the most relevant checks before push:

```bash
bash scripts/check_feature_boundaries.sh
.venv/bin/pio check -e default
git diff --submodule=log -- open-x4-sdk
git -C open-x4-sdk status --short --branch
```

For CI triage after push:

```bash
gh run list --limit 10
gh run view <run-id> --log-failed
```

## Failure patterns

### Submodule checkout fails in GitHub Actions

Symptom:

```text
fatal: remote error: upload-pack: not our ref <sha>
```

Meaning:

- the parent repo points at a submodule commit not reachable from the remote URL in `.gitmodules`

Fix:

1. push the submodule branch containing that SHA to the fork remote
2. ensure `.gitmodules` points at the fork remote
3. rerun Actions

### Old nightly or scheduled build fails on missing files

Check the failing run's `headSha` first. If it is older than current `fork-drift`, do not assume the current tree is broken.

## Guardrails

- Do not change `.gitmodules` back to upstream unless the submodule SHA is guaranteed to exist upstream.
- Do not leave `open-x4-sdk` on a detached HEAD without publishing the referenced commit.
- Do not amend the parent repo to a new submodule SHA until that SHA is visible on the fork remote.
- Prefer merge commits for upstream sync unless the user explicitly asks for rebasing.
