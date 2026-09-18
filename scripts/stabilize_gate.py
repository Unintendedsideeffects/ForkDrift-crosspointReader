#!/usr/bin/env python3
"""Release gate: Home must look the same after boot, Helm, Settings, and File Transfer.

Zero unexpected resets and zero [ERR] lines. Grimmory/Always HTTPS at most once
per boot (auth failures are cached). USB passthrough flaps are reported separately
from device resets.

  uv run python scripts/stabilize_gate.py --outdir runs/stabilize-gate
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from device_walk import DeviceLink, autodetect_port

SHELF_FETCH = re.compile(r"Library shelf GET")
AUTH_BACKOFF = re.compile(r"Library shelf refresh skipped: auth backoff")
ALWAYS_START = re.compile(r"Starting background web server on existing WiFi")


def snapshot(link: DeviceLink, label: str) -> dict:
    name, stack = link.activity()
    heap = link.heap_profile()
    row = {
        "label": label,
        "activity": name,
        "stack": stack,
        "free": heap["free"],
        "largest": heap["largest"],
        "min_free": heap["min_free"],
        "t_ms": heap["t"],
        "errors": len(link.errors),
        "resets": len(link.resets),
        "usb_flaps": len(link.usb_flaps),
    }
    print(
        f"  [{label}] activity={name} free={heap['free']} largest={heap['largest']} "
        f"err={row['errors']} rst={row['resets']} usb={row['usb_flaps']}",
        flush=True,
    )
    return row


def require_home(row: dict) -> None:
    if row["activity"] != "Home":
        raise RuntimeError(f"{row['label']} ended in {row['activity']!r}, expected Home")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (default: autodetect)")
    parser.add_argument("--outdir", type=Path, default=Path("runs/stabilize-gate"))
    args = parser.parse_args()

    args.outdir.mkdir(parents=True, exist_ok=True)
    log_path = args.outdir / "serial.log"
    link = DeviceLink(args.port or autodetect_port(), log_path)
    rows: list[dict] = []
    try:
        link.drain(0.5)
        link.ping()
        link.go_home()
        link.drain(22.0)
        rows.append(snapshot(link, "clean-boot"))
        require_home(rows[-1])

        print("Helm", flush=True)
        link.press("CONFIRM", settle_s=8.0)
        link.wait_activity("EpubReader", 40.0)
        rows.append(snapshot(link, "in-helm"))
        link.goto("Home", settle_s=8.0)
        link.wait_activity("Home", 40.0)
        link.drain(6.0)
        rows.append(snapshot(link, "post-helm"))
        require_home(rows[-1])

        print("Settings", flush=True)
        link.goto("Settings", settle_s=3.0)
        link.wait_activity("Settings", 20.0)
        rows.append(snapshot(link, "in-settings"))
        link.goto("Home", settle_s=6.0)
        link.wait_activity("Home", 30.0)
        link.drain(6.0)
        rows.append(snapshot(link, "post-settings"))
        require_home(rows[-1])

        print("File Transfer", flush=True)
        link.goto("FileTransfer", settle_s=3.0)
        ft_name = link.wait_activity(("CrossPointWebServer", "NetworkModeSelection"), 20.0)
        rows.append(snapshot(link, "in-file-transfer"))
        if rows[-1]["activity"] not in ("CrossPointWebServer", "NetworkModeSelection"):
            raise RuntimeError(f"File Transfer landed in {ft_name!r}")
        link.goto("Home", settle_s=8.0)
        link.wait_activity("Home", 30.0)
        link.drain(8.0)
        rows.append(snapshot(link, "post-file-transfer"))
        require_home(rows[-1])
    except (TimeoutError, RuntimeError) as exc:
        print(f"GATE FAIL: {exc}", file=sys.stderr)
        (args.outdir / "gate.json").write_text(json.dumps({"ok": False, "error": str(exc), "rows": rows}, indent=2))
        return 1
    finally:
        serial_text = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
        link.close()

    shelf_fetches = len(SHELF_FETCH.findall(serial_text))
    auth_skips = len(AUTH_BACKOFF.findall(serial_text))
    always_starts = len(ALWAYS_START.findall(serial_text))
    home_rows = [row for row in rows if row["label"].startswith("post-") or row["label"] == "clean-boot"]
    report = {
        "ok": False,
        "rows": rows,
        "shelf_https_fetches": shelf_fetches,
        "auth_backoff_skips": auth_skips,
        "always_adopts": always_starts,
        "errors": link.errors,
        "resets": link.resets,
        "usb_flaps": link.usb_flaps,
        "home_heap": [
            {"label": row["label"], "free": row["free"], "largest": row["largest"]} for row in home_rows
        ],
    }

    failures: list[str] = []
    if link.resets:
        failures.append(f"{len(link.resets)} unexpected reset(s)")
    if link.errors:
        failures.append(f"{len(link.errors)} [ERR] line(s)")
    if shelf_fetches > 1:
        failures.append(f"Grimmory HTTPS ran {shelf_fetches} times (want <= 1 per boot)")

    report["ok"] = not failures
    (args.outdir / "gate.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({k: report[k] for k in ("ok", "shelf_https_fetches", "auth_backoff_skips", "always_adopts")}, indent=2))
    if failures:
        print("GATE FAIL:", file=sys.stderr)
        for item in failures:
            print(f"  {item}", file=sys.stderr)
        return 1
    if link.usb_flaps:
        print(f"USB flaps (not a gate failure): {len(link.usb_flaps)}", file=sys.stderr)
    print("GATE PASS: Home matches across boot, Helm, Settings, File Transfer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
