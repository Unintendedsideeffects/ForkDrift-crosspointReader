#!/usr/bin/env python3
"""Drive a connected X4 over USB serial: inject buttons, capture screenshots.

Host-side counterpart to the firmware serial command handler in src/main.cpp:

  CMD:PING           -> PONG
  CMD:BTN:<NAME>     -> BTN_OK:<NAME> | BTN_ERR:<NAME>   (injects logical button)
  CMD:SCREENSHOT     -> SCREENSHOT_START:<n> + raw 1bpp framebuffer + SCREENSHOT_END

Together these allow simulator-style interaction runs on real hardware:
press a button, wait for the e-ink refresh, pull the framebuffer as a PNG,
and scan the serial log for errors — all over a single USB cable.

Usage:
  uv run python scripts/device_walk.py ping
  uv run python scripts/device_walk.py shot home.png
  uv run python scripts/device_walk.py press CONFIRM DOWN CONFIRM
  uv run python scripts/device_walk.py wificred "MySSID" "MyPassword"
  uv run python scripts/device_walk.py run scripts/walks/example.walk --outdir runs/smoke

Walk file DSL (one command per line, '#' starts a comment):
  press <BTN> [settle_sec]     inject button, wait settle seconds (default 2.0)
  shot <name>                  capture screenshot to NNN-<name>.png in outdir
  sleep <seconds>
  expect <timeout_sec> <regex> wait until a serial log line matches regex

Buttons: BACK CONFIRM LEFT RIGHT UP DOWN PAGEBACK PAGEFWD
"""

import argparse
import glob
import re
import sys
import time
from pathlib import Path

import serial

try:
    from PIL import Image
except ImportError:  # PNG decode degrades to raw dump
    Image = None

BAUD = 115200
FRAME_W, FRAME_H = 800, 480  # raw framebuffer is landscape; rotate for portrait
DEFAULT_SETTLE_S = 2.0  # e-ink refresh + render time after a button press
ERROR_MARKERS = ("[ERR]", "Guru Meditation", "abort()", "Backtrace:", "panic'ed")
BUTTONS = ("BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN", "PAGEBACK", "PAGEFWD")


def autodetect_port() -> str:
    candidates = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if not candidates:
        sys.exit("No serial device found (/dev/ttyACM*, /dev/ttyUSB*). Is the X4 plugged in?")
    return candidates[0]


class DeviceLink:
    """Sequential request/response link that tees all log traffic to a file."""

    def __init__(self, port: str, log_path: Path | None):
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.log_file = open(log_path, "a", encoding="utf-8") if log_path else None
        self.errors: list[str] = []

    def close(self):
        self.ser.close()
        if self.log_file:
            self.log_file.close()

    def _record(self, line: str):
        if self.log_file:
            self.log_file.write(line + "\n")
            self.log_file.flush()
        if any(marker in line for marker in ERROR_MARKERS):
            self.errors.append(line)
            print(f"  !! {line}", file=sys.stderr)

    def read_line(self, timeout_s: float) -> str | None:
        """Read one text line, recording it; None on timeout."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                self._record(line)
                return line
        return None

    def drain(self, duration_s: float = 0.0):
        """Consume pending log lines (recording them) for duration_s."""
        deadline = time.monotonic() + duration_s
        while True:
            raw = self.ser.readline()
            if raw:
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    self._record(line)
            elif time.monotonic() >= deadline:
                return

    def command(self, cmd: str, expect: re.Pattern, timeout_s: float = 5.0) -> str:
        """Send CMD:<cmd> and wait for a line matching expect (logging the rest)."""
        self.ser.write(f"CMD:{cmd}\n".encode())
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.read_line(deadline - time.monotonic())
            if line and expect.search(line):
                return line
        raise TimeoutError(f"No response matching {expect.pattern!r} to CMD:{cmd}")

    def ping(self):
        self.command("PING", re.compile(r"^PONG$"))

    def press(self, button: str, settle_s: float = DEFAULT_SETTLE_S):
        button = button.upper()
        if button not in BUTTONS:
            raise ValueError(f"Unknown button {button!r}; expected one of {BUTTONS}")
        line = self.command(f"BTN:{button}", re.compile(rf"^BTN_(OK|ERR):{button}$"))
        if line.startswith("BTN_ERR"):
            raise RuntimeError(f"Firmware rejected button {button}")
        self.drain(settle_s)

    def screenshot(self, out_path: Path):
        self.ser.write(b"CMD:SCREENSHOT\n")
        # Wait for the size header, recording interleaved log lines.
        deadline = time.monotonic() + 10.0
        size = None
        while time.monotonic() < deadline:
            line = self.read_line(deadline - time.monotonic())
            if line and line.startswith("SCREENSHOT_START:"):
                size = int(line.split(":", 1)[1])
                break
        if size is None:
            raise TimeoutError("No SCREENSHOT_START header from device")

        data = b""
        deadline = time.monotonic() + 15.0
        while len(data) < size and time.monotonic() < deadline:
            chunk = self.ser.read(size - len(data))
            if chunk:
                data += chunk
        if len(data) != size:
            raise TimeoutError(f"Framebuffer truncated: {len(data)}/{size} bytes")
        self.read_line(2.0)  # consume SCREENSHOT_END

        out_path.parent.mkdir(parents=True, exist_ok=True)
        if Image and size == FRAME_W * FRAME_H // 8:
            img = Image.frombytes("1", (FRAME_W, FRAME_H), data)
            img = img.transpose(Image.ROTATE_270)
            img.save(out_path)
        else:
            out_path = out_path.with_suffix(".raw")
            out_path.write_bytes(data)
        print(f"  saved {out_path}")

    def set_wifi_credential(self, ssid: str, password: str):
        line = self.command(f"WIFICRED:{ssid}\t{password}", re.compile(r"^WIFICRED_(OK|ERR):"), timeout_s=10.0)
        if line.startswith("WIFICRED_ERR"):
            raise RuntimeError(f"Firmware rejected credential update: {line}")

    def expect_log(self, pattern: str, timeout_s: float) -> str:
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.read_line(deadline - time.monotonic())
            if line and regex.search(line):
                return line
        raise TimeoutError(f"No log line matching {pattern!r} within {timeout_s}s")


def run_walk(link: DeviceLink, walk_path: Path, outdir: Path) -> int:
    steps = []
    for lineno, raw in enumerate(walk_path.read_text().splitlines(), start=1):
        text = raw.split("#", 1)[0].strip()
        if text:
            steps.append((lineno, text.split()))

    shot_index = 0
    for lineno, parts in steps:
        verb, args = parts[0].lower(), parts[1:]
        print(f"[{walk_path.name}:{lineno}] {verb} {' '.join(args)}")
        if verb == "press":
            settle = float(args[1]) if len(args) > 1 else DEFAULT_SETTLE_S
            link.press(args[0], settle)
        elif verb == "shot":
            shot_index += 1
            link.screenshot(outdir / f"{shot_index:03d}-{args[0]}.png")
        elif verb == "sleep":
            link.drain(float(args[0]))
        elif verb == "expect":
            link.expect_log(" ".join(args[1:]), float(args[0]))
        else:
            sys.exit(f"{walk_path}:{lineno}: unknown verb {verb!r}")

    print(f"\nWalk complete: {shot_index} screenshots in {outdir}")
    if link.errors:
        print(f"{len(link.errors)} error line(s) seen on serial:", file=sys.stderr)
        for err in link.errors:
            print(f"  {err}", file=sys.stderr)
        return 1
    print("No error markers in serial log.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: autodetect)")
    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("ping", help="check the firmware responds to commands")

    p_shot = sub.add_parser("shot", help="capture one screenshot")
    p_shot.add_argument("output", type=Path)

    p_press = sub.add_parser("press", help="inject one or more button presses")
    p_press.add_argument("buttons", nargs="+", metavar="BTN")
    p_press.add_argument("--settle", type=float, default=DEFAULT_SETTLE_S)

    p_cred = sub.add_parser("wificred", help="set or update a saved WiFi credential")
    p_cred.add_argument("ssid")
    p_cred.add_argument("password")

    p_run = sub.add_parser("run", help="execute a walk file")
    p_run.add_argument("walk", type=Path)
    p_run.add_argument("--outdir", type=Path, default=Path("runs/walk"))

    args = parser.parse_args()
    port = args.port or autodetect_port()

    outdir = getattr(args, "outdir", Path("."))
    log_path = outdir / "serial.log" if args.action == "run" else None
    if log_path:
        outdir.mkdir(parents=True, exist_ok=True)

    link = DeviceLink(port, log_path)
    try:
        link.drain(0.3)  # flush boot/backlog noise
        if args.action == "ping":
            link.ping()
            print("PONG — device is responding")
            return 0
        if args.action == "shot":
            link.screenshot(args.output)
            return 0
        if args.action == "press":
            for btn in args.buttons:
                print(f"press {btn}")
                link.press(btn, args.settle)
            return 0
        if args.action == "wificred":
            link.set_wifi_credential(args.ssid, args.password)
            print(f"Credential for {args.ssid!r} saved on device")
            return 0
        if args.action == "run":
            link.ping()
            return run_walk(link, args.walk, args.outdir)
        return 2
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
