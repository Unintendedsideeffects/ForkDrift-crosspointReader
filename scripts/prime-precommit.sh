#!/bin/bash
# Prime the pre-commit build cache so the next `git commit` completes in seconds.
#
# The pre-commit hook (scripts/hooks/pre-commit) caches build results keyed by
# the staged-tree OID produced by `git write-tree` AFTER all hook mutations
# (clang-format, I18n header regeneration, generated-header pruning).  Running
# `pio run` yourself never writes that cache file, so honoring the hook means
# paying a redundant rebuild — and `git commit --no-verify` becomes the fast
# path.  This script removes that incentive by invoking the hook directly.
#
# Because the script IS the hook, the cache key is necessarily identical to the
# one the hook would compute on a real commit.  There is no way for them to
# drift.
#
# Usage:
#   scripts/prime-precommit.sh        # prime what is currently staged
#   scripts/prime-precommit.sh -a     # git add -A first, then prime
#   scripts/prime-precommit.sh --all  # same as -a
#   scripts/prime-precommit.sh -h     # show this help
#
# On success: the next `git commit` (without --no-verify) hits the cache and
#             returns in seconds instead of minutes.
# On failure: the build error is reported — the same error that would have
#             blocked the commit.  Fix it, restage, and prime again.
#
# Note: this script mutates the index (clang-format re-stages, generated-header
# pruning).  That is not a side-effect — it is exactly what `git commit` would
# have done, moved earlier where you can see it.  Restaging files after priming
# invalidates the cache, by design.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

HOOK="scripts/hooks/pre-commit"

usage() {
    sed -n '/^# Usage:/,/^#$/p' "$0" | sed 's/^# \?//'
    exit 0
}

ADD_ALL=0
for arg in "$@"; do
    case "$arg" in
        -a|--all)  ADD_ALL=1 ;;
        -h|--help) usage ;;
        *)
            echo "prime-precommit: unknown option: $arg" >&2
            echo "Usage: $0 [-a|--all] [-h|--help]" >&2
            exit 1
            ;;
    esac
done

if [ "$ADD_ALL" = "1" ]; then
    echo "prime-precommit: staging all changes (git add -A)..."
    git add -A
fi

if git diff --cached --quiet; then
    echo "prime-precommit: nothing staged. Stage your changes first, or pass -a / --all." >&2
    echo "  git add <files>            # stage specific files" >&2
    echo "  $0 -a                      # stage everything and prime" >&2
    exit 1
fi

BUILD_PROFILE="${CROSSPOINT_PRECOMMIT_PROFILE:-full}"
PRE_RUN_KEY="$(git write-tree)"

echo "prime-precommit: profile=${BUILD_PROFILE}, pre-run tree=${PRE_RUN_KEY}"
echo "prime-precommit: invoking hook — this will run the full firmware build if not cached..."
echo ""

"$HOOK"
HOOK_EXIT=$?

if [ "$HOOK_EXIT" -eq 0 ]; then
    POST_RUN_KEY="$(git write-tree)"
    CACHE_FILE=".cache/build-results/${BUILD_PROFILE}-${POST_RUN_KEY}"
    echo ""
    echo "prime-precommit: cache primed successfully."
    echo "  post-run tree : ${POST_RUN_KEY}"
    echo "  cache file    : ${CACHE_FILE}"
    if [ "$PRE_RUN_KEY" != "$POST_RUN_KEY" ]; then
        echo "  (pre-run tree was ${PRE_RUN_KEY} — hook mutations changed the index)"
    fi
    echo "  The next 'git commit' will hit the cache and finish in seconds."
else
    echo "" >&2
    echo "prime-precommit: build FAILED (exit ${HOOK_EXIT}) — this staged tree would have been blocked by the hook." >&2
    echo "  Fix the error, restage, and run this script again." >&2
    exit "$HOOK_EXIT"
fi
