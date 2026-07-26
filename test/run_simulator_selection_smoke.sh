#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
declare -a SMOKE_LOGS=()

cleanup() {
  if ((${#SMOKE_LOGS[@]} > 0)); then
    rm -f -- "${SMOKE_LOGS[@]}"
  fi
}
trap cleanup EXIT

run_case() {
  local name="$1"
  local expected_loads="$2"
  shift 2
  local log_file
  log_file="$(mktemp "${TMPDIR:-/tmp}/forkdrift-selection-smoke.XXXXXX.log")"
  SMOKE_LOGS+=("$log_file")

  echo "Testing $name..."
  if ! env FORKDRIFT_SIMULATOR_SMOKE_SELECTION_COLD=1 "$@" \
    uv run python scripts/run_simulator_smoke_test.py --no-build >"$log_file" 2>&1; then
    cat "$log_file" >&2
    return 1
  fi
  if ! grep -Fq "SMOKE_SELECTION_CONTENT_LOADS=$expected_loads" "$log_file"; then
    echo "$name did not report exactly the expected selection entry load count ($expected_loads)" >&2
    cat "$log_file" >&2
    return 1
  fi
  if [[ "$expected_loads" == "0" ]] && grep -Eq 'SMOKE_SELECTION_CONTENT_LOADS=[1-9]' "$log_file"; then
    echo "$name unexpectedly reloaded page content" >&2
    cat "$log_file" >&2
    return 1
  fi
  if ! grep -Fq "Simulator smoke test passed" "$log_file"; then
    echo "$name did not complete the simulator smoke" >&2
    cat "$log_file" >&2
    return 1
  fi
  grep -E 'SMOKE_SELECTION_(INDEX_RETAINED|CONTENT_LOADS)|SELECTION_INDEX_FALLBACK|Simulator smoke test passed' \
    "$log_file"
}

cd "$ROOT_DIR"
echo "Building simulator..."
scripts/pio-locked.sh run -e simulator

run_case "healthy retained index" 0
run_case "forced low-heap fallback" 1 FORKDRIFT_SIMULATOR_SELECTION_INDEX_FALLBACK=1

echo "All selection smoke assertions passed"
