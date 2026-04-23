#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME="default"
MONITOR_SPEED="115200"
UPLOAD_FIRST=1
PORT=""

usage() {
  cat <<'EOF'
Usage: scripts/pio_debug.sh [options]

Uploads firmware with PlatformIO, starts the serial monitor, and tees output to
a timestamped log file under .logs/serial/.

Options:
  --no-upload        Skip firmware upload and only start the monitor
  --env NAME         PlatformIO environment to use (default: default)
  --port DEVICE      Serial device path to pass to PlatformIO monitor/upload
  --baud RATE        Monitor baud rate (default: 115200)
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-upload)
      UPLOAD_FIRST=0
      shift
      ;;
    --env)
      ENV_NAME="${2:?missing env name}"
      shift 2
      ;;
    --port)
      PORT="${2:?missing port path}"
      shift 2
      ;;
    --baud)
      MONITOR_SPEED="${2:?missing baud rate}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

cd "$ROOT_DIR"

LOG_DIR="$ROOT_DIR/.logs/serial"
mkdir -p "$LOG_DIR"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/${TIMESTAMP}-${ENV_NAME}.log"

PIO_ARGS=(-e "$ENV_NAME")
if [[ -n "$PORT" ]]; then
  PIO_ARGS+=(--upload-port "$PORT")
fi

MONITOR_ARGS=(-e "$ENV_NAME" -b "$MONITOR_SPEED")
if [[ -n "$PORT" ]]; then
  MONITOR_ARGS+=(--port "$PORT")
fi

echo "Repo: $ROOT_DIR"
echo "Environment: $ENV_NAME"
if [[ -n "$PORT" ]]; then
  echo "Port: $PORT"
fi
echo "Serial log: $LOG_FILE"

if [[ "$UPLOAD_FIRST" -eq 1 ]]; then
  echo "Uploading firmware..."
  uv run pio run "${PIO_ARGS[@]}" -t upload
fi

echo "Starting serial monitor..."
echo "Press Ctrl-C to stop. Logs will remain in $LOG_FILE"

uv run pio device monitor "${MONITOR_ARGS[@]}" | tee "$LOG_FILE"
