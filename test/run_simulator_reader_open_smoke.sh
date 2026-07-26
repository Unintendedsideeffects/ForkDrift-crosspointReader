#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/forkdrift-reader-open.XXXXXX")"
trap 'rm -rf -- "$FIXTURE_ROOT"' EXIT

mkdir -p "$FIXTURE_ROOT/books"
cp "$ROOT_DIR/test/epubs/test_tables.epub" "$FIXTURE_ROOT/books/fixture.epub"
printf '# Markdown reader\n\nA bounded fixture.\n' >"$FIXTURE_ROOT/books/fixture.md"
head -c $((80 * 1024)) /dev/zero | tr '\0' 'a' >"$FIXTURE_ROOT/books/fixture-fragmented.md"
head -c $((96 * 1024 + 1)) /dev/zero | tr '\0' 'a' >"$FIXTURE_ROOT/books/fixture-oversized.md"
printf 'Plain text reader\nA second line.\n' >"$FIXTURE_ROOT/books/fixture.txt"
printf '\x58\x54\x43\x00\x01\x00\x01\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x38\x00\x00\x00\x00\x00\x00\x00\x48\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x48\x00\x00\x00\x00\x00\x00\x00\x17\x00\x00\x00\x08\x00\x01\x00\x58\x54\x47\x00\x08\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xaa' >"$FIXTURE_ROOT/books/fixture.xtc"

cd "$ROOT_DIR"
scripts/pio-locked.sh run -e simulator

run_case() {
  local mode="$1"
  local extension="$2"
  local log_file="$FIXTURE_ROOT/$mode-$extension.log"
  local -a env_args=(FORKDRIFT_SIMULATOR_READER_OPEN_ONLY=1)
  local expected_marker="SMOKE_READER_OPENED"
  if [[ "$mode" == "fragmented" ]]; then
    env_args+=(SIM_HEAP_LARGEST=1 FORKDRIFT_SIMULATOR_EXPECT_READER_LOAD_FAILURE=1)
    expected_marker="SMOKE_READER_LOAD_FAILED_RECOVERABLY"
  fi

  if ! env "${env_args[@]}" uv run python scripts/run_simulator_smoke_test.py --no-build --page-turns 0 \
    --fs-root "$FIXTURE_ROOT" --book "/books/fixture.$extension" >"$log_file" 2>&1; then
    cat "$log_file" >&2
    return 1
  fi
  grep -F "$expected_marker" "$log_file"
  grep -F "Simulator smoke test passed" "$log_file"
  if [[ "$mode" == "fragmented" ]]; then
    grep -F "READER_ALLOC_REJECT" "$log_file"
  fi
}

for extension in epub md txt xtc; do
  run_case healthy "$extension"
  run_case fragmented "$extension"
done

run_markdown_rejection() {
  local fixture="$1"
  local largest="$2"
  local log_file="$FIXTURE_ROOT/markdown-$fixture.log"
  if ! env FORKDRIFT_SIMULATOR_READER_OPEN_ONLY=1 FORKDRIFT_SIMULATOR_EXPECT_READER_LOAD_FAILURE=1 \
    SIM_HEAP_LARGEST="$largest" uv run python scripts/run_simulator_smoke_test.py --no-build --page-turns 0 \
    --fs-root "$FIXTURE_ROOT" --book "/books/$fixture" >"$log_file" 2>&1; then
    cat "$log_file" >&2
    return 1
  fi
  grep -F "MARKDOWN_ADMISSION_REJECT" "$log_file"
  grep -F "SMOKE_READER_LOAD_FAILED_RECOVERABLY" "$log_file"
  grep -F "Simulator smoke test passed" "$log_file"
}

run_markdown_rejection fixture-fragmented.md 4096
run_markdown_rejection fixture-oversized.md 330000

echo "All reader-open smoke assertions passed"
