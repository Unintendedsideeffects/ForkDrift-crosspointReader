#!/usr/bin/env bash
# Launch the ForkDrift simulator over noVNC.
# Access at http://<tailscale-ip>:6081/vnc.html after running this script.
#
# Uses display :99, VNC port 5901, websockify port 6081 to avoid colliding
# with any existing x11vnc/noVNC session on the machine (typically :5/5900/6080).
#
# Usage:
#   ./scripts/sim_vnc.sh          # start (builds if binary missing)
#   ./scripts/sim_vnc.sh stop     # kill all managed processes
#   ./scripts/sim_vnc.sh rebuild  # clean build then start

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
# PlatformIO puts the native binary in .cache when build_dir is overridden
BINARY_CACHE="/home/$(whoami)/.cache/crosspoint-pio-build/simulator/program"
BINARY_LOCAL="$REPO/.pio/build/simulator/program"
BINARY="${BINARY_CACHE}"
[[ -f "$BINARY_LOCAL" ]] && BINARY="$BINARY_LOCAL"
DISPLAY_NUM=99
VNC_PORT=5901
WS_PORT=6081
NOVNC_DIR=/opt/novnc
PIDFILE=/tmp/forkdrift-sim-vnc.pid

stop_all() {
  if [[ -f "$PIDFILE" ]]; then
    while IFS= read -r pid; do
      kill "$pid" 2>/dev/null || true
    done < "$PIDFILE"
    rm -f "$PIDFILE"
  fi
  # Belt-and-suspenders: kill by name on our display
  pkill -f "Xvfb :${DISPLAY_NUM}" 2>/dev/null || true
  pkill -f "x11vnc.*:${DISPLAY_NUM}" 2>/dev/null || true
  pkill -f "websockify.*${WS_PORT}" 2>/dev/null || true
  pkill -f "$BINARY" 2>/dev/null || true
  echo "Stopped."
}

if [[ "${1:-}" == "stop" ]]; then
  stop_all
  exit 0
fi

if [[ "${1:-}" == "rebuild" ]]; then
  (cd "$REPO" && uv run pio run -e simulator)
fi

# Build if binary is missing
if [[ ! -f "$BINARY" ]]; then
  echo "Simulator binary not found — building..."
  (cd "$REPO" && uv run pio run -e simulator)
  # Re-resolve after build
  [[ -f "$BINARY_LOCAL" ]] && BINARY="$BINARY_LOCAL" || BINARY="$BINARY_CACHE"
fi

# Kill any leftover session from a prior run
stop_all 2>/dev/null || true

echo "Starting Xvfb on :${DISPLAY_NUM}..."
Xvfb ":${DISPLAY_NUM}" -screen 0 800x800x24 &
XVFB_PID=$!
sleep 0.5

echo "Launching simulator..."
DISPLAY=":${DISPLAY_NUM}" "$BINARY" &
SIM_PID=$!
sleep 1

echo "Starting x11vnc on port ${VNC_PORT}..."
x11vnc -display ":${DISPLAY_NUM}" -nopw -rfbport "$VNC_PORT" \
       -listen localhost -forever -quiet &
X11VNC_PID=$!
sleep 0.5

echo "Starting websockify on port ${WS_PORT}..."
websockify --web "$NOVNC_DIR" "$WS_PORT" "localhost:${VNC_PORT}" &
WS_PID=$!

printf '%s\n' "$XVFB_PID" "$SIM_PID" "$X11VNC_PID" "$WS_PID" > "$PIDFILE"

TAILSCALE_IP=$(tailscale ip 2>/dev/null | head -1 || echo "<tailscale-ip>")
echo ""
echo "================================================"
echo " ForkDrift simulator running"
echo " Open in browser:"
echo "   http://${TAILSCALE_IP}:${WS_PORT}/vnc.html"
echo ""
echo " Stop with:  ./scripts/sim_vnc.sh stop"
echo "================================================"
