#!/usr/bin/env bash

set -euo pipefail
shopt -s nullglob

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME="default"
REFRESH_DB=0
CLANGD_ARGS=()

usage() {
  cat <<'EOF'
Usage: scripts/clangd.sh [options] [-- <clangd args...>]

Launch clangd with the PlatformIO toolchain whitelisted for query-driver so the
ESP32-C3 GCC standard library and sysroot headers resolve correctly.

Options:
  --refresh-db      Regenerate compile_commands.json before launching clangd
  --env NAME        PlatformIO environment to use when refreshing the database
                    (default: default)
  -h, --help        Show this help

Examples:
  scripts/clangd.sh
  scripts/clangd.sh --refresh-db
  scripts/clangd.sh -- --check=src/main.cpp
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --refresh-db)
      REFRESH_DB=1
      shift
      ;;
    --env)
      ENV_NAME="${2:?missing env name}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      CLANGD_ARGS+=("$@")
      break
      ;;
    *)
      CLANGD_ARGS+=("$1")
      shift
      ;;
  esac
done

cd "$ROOT_DIR"

if [[ "$REFRESH_DB" -eq 1 || ! -f compile_commands.json ]]; then
  echo "Refreshing compile_commands.json for env '$ENV_NAME'..."
  uv run pio run -e "$ENV_NAME" -t compiledb >/dev/null
fi

if [[ ! -f compile_commands.json ]]; then
  echo "compile_commands.json is missing. Run scripts/clangd.sh --refresh-db." >&2
  exit 1
fi

query_driver_globs=()
for toolchain_dir in "$HOME/.platformio/packages"/toolchain-*; do
  [[ -d "$toolchain_dir/bin" ]] || continue
  query_driver_globs+=("$toolchain_dir/bin/*")
done

if [[ ${#query_driver_globs[@]} -eq 0 ]]; then
  echo "No PlatformIO toolchains were found under $HOME/.platformio/packages." >&2
  exit 1
fi

QUERY_DRIVER=$(IFS=,; echo "${query_driver_globs[*]}")

exec clangd \
  --compile-commands-dir="$ROOT_DIR" \
  --query-driver="$QUERY_DRIVER" \
  "${CLANGD_ARGS[@]}"
