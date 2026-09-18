#!/usr/bin/env python3
"""Behavior gate: second reader open, a page turn, then authenticated Grimmory.

Heap figures are recorded as diagnostics. Pass/fail is activity and fetch
behavior: Helm must reopen, PAGEFWD must stay in the reader, and OPDS must
perform a catalog GET that parses entries.

  uv run python scripts/reentry_gate.py --outdir runs/reentry-gate
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from device_walk import DeviceLink, autodetect_port

CATALOG_GET = re.compile(r"Catalog GET(?! failed)")
CATALOG_PARSED = re.compile(r"Catalog parsed: (\d+) entries")
CATALOG_FAILED = re.compile(r"Catalog GET failed: (\S+(?:\s\S+)*)")
INFLATE_FAIL = re.compile(r"Failed to pre-allocate inflate window|Failed to reserve inflate window")


def snapshot(link: DeviceLink, label: str, *, activity: str | None = None, heap: bool = True) -> dict:
    if activity is None:
        name, stack = link.activity()
    else:
        name, stack = activity, 0
    row = {
        "label": label,
        "activity": name,
        "stack": stack,
        "free": None,
        "largest": None,
        "min_free": None,
        "t_ms": None,
        "errors": len(link.errors),
        "resets": len(link.resets),
        "usb_flaps": len(link.usb_flaps),
    }
    if heap:
        try:
            profile = link.heap_profile()
            row["free"] = profile["free"]
            row["largest"] = profile["largest"]
            row["min_free"] = profile["min_free"]
            row["t_ms"] = profile["t"]
        except TimeoutError:
            row["heap_timeout"] = True
            print(
                f"  [{label}] activity={name} heap=timeout err={row['errors']} rst={row['resets']}",
                flush=True,
            )
            return row
    print(
        f"  [{label}] activity={name} free={row['free']} largest={row['largest']} "
        f"err={row['errors']} rst={row['resets']} usb={row['usb_flaps']}",
        flush=True,
    )
    return row


def ping_wake(link: DeviceLink) -> None:
    try:
        link.ping()
    except TimeoutError:
        try:
            link.ping()
        except TimeoutError:
            pass


def require_activity(row: dict, expected: str | tuple[str, ...]) -> None:
    names = (expected,) if isinstance(expected, str) else expected
    if row["activity"] not in names:
        raise RuntimeError(f"{row['label']} ended in {row['activity']!r}, expected {names!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (default: autodetect)")
    parser.add_argument("--outdir", type=Path, default=Path("runs/reentry-gate"))
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
        ping_wake(link)
        rows.append(snapshot(link, "clean-home"))
        require_activity(rows[-1], "Home")

        print("Helm 1", flush=True)
        ping_wake(link)
        link.press("CONFIRM", settle_s=8.0)
        link.wait_activity("EpubReader", 300.0)
        link.drain(1.0)
        rows.append(snapshot(link, "helm-1", activity="EpubReader", heap=False))
        require_activity(rows[-1], "EpubReader")
        ping_wake(link)
        link.goto("Home", settle_s=2.0)
        ping_wake(link)
        link.wait_activity("Home", 40.0)
        link.drain(1.0)
        rows.append(snapshot(link, "home-after-helm-1", activity="Home", heap=False))
        require_activity(rows[-1], "Home")

        print("Helm 2", flush=True)
        ping_wake(link)
        link.press("CONFIRM", settle_s=8.0)
        link.wait_activity("EpubReader", 90.0)
        link.drain(1.0)
        rows.append(snapshot(link, "helm-2", activity="EpubReader", heap=False))
        require_activity(rows[-1], "EpubReader")

        print("page turn", flush=True)
        errors_before_turn = len(link.errors)
        resets_before_turn = len(link.resets)
        ping_wake(link)
        link.press("PAGEFWD", settle_s=8.0)
        link.wait_activity("EpubReader", 20.0)
        rows.append(snapshot(link, "helm-2-turned", activity="EpubReader", heap=False))
        require_activity(rows[-1], "EpubReader")
        if len(link.resets) > resets_before_turn:
            raise RuntimeError("page turn reset the device")
        if len(link.errors) > errors_before_turn:
            raise RuntimeError("page turn logged [ERR]")

        ping_wake(link)
        link.goto("Home", settle_s=2.0)
        ping_wake(link)
        link.wait_activity("Home", 40.0)
        link.drain(1.0)
        rows.append(snapshot(link, "home-after-helm-2", activity="Home", heap=False))
        require_activity(rows[-1], "Home")

        print("Grimmory", flush=True)
        ping_wake(link)
        link.goto("Opds", settle_s=4.0)
        ping_wake(link)
        link.wait_activity("OpdsBookBrowser", 20.0)
        link.drain(18.0)
        ping_wake(link)
        rows.append(snapshot(link, "grimmory"))
        require_activity(rows[-1], "OpdsBookBrowser")
    except (TimeoutError, RuntimeError) as exc:
        print(f"GATE FAIL: {exc}", file=sys.stderr)
        (args.outdir / "gate.json").write_text(
            json.dumps({"ok": False, "error": str(exc), "rows": rows}, indent=2)
        )
        return 1
    finally:
        serial_text = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
        link.close()

    parsed_counts = [int(match) for match in CATALOG_PARSED.findall(serial_text)]
    fetch_failures = CATALOG_FAILED.findall(serial_text)
    catalog_gets = len(CATALOG_GET.findall(serial_text))
    inflate_fails = INFLATE_FAIL.findall(serial_text)
    report = {
        "ok": False,
        "rows": rows,
        "catalog_gets": catalog_gets,
        "catalog_parsed_counts": parsed_counts,
        "catalog_fetch_failures": fetch_failures,
        "inflate_window_failures": inflate_fails,
        "errors": link.errors,
        "resets": link.resets,
        "usb_flaps": link.usb_flaps,
        "heap": [{"label": row["label"], "free": row["free"], "largest": row["largest"]} for row in rows],
    }

    failures: list[str] = []
    if link.resets:
        failures.append(f"{len(link.resets)} unexpected reset(s)")
    if link.errors:
        failures.append(f"{len(link.errors)} [ERR] line(s)")
    if inflate_fails:
        failures.append("inflate window reservation failed (second open structurally broken)")
    if catalog_gets < 1:
        failures.append("Grimmory did not perform a catalog GET")
    elif not parsed_counts or parsed_counts[-1] < 1:
        if fetch_failures:
            failures.append(f"Grimmory catalog GET failed: {fetch_failures[-1]}")
        else:
            failures.append("Grimmory GET did not parse any entries")

    report["ok"] = not failures
    (args.outdir / "gate.json").write_text(json.dumps(report, indent=2))
    print(
        json.dumps(
            {
                "ok": report["ok"],
                "catalog_gets": catalog_gets,
                "catalog_parsed_counts": parsed_counts,
                "catalog_fetch_failures": fetch_failures,
            },
            indent=2,
        )
    )
    if failures:
        print("GATE FAIL:", file=sys.stderr)
        for item in failures:
            print(f"  {item}", file=sys.stderr)
        return 1
    if link.usb_flaps:
        print(f"USB flaps (not a gate failure): {len(link.usb_flaps)}", file=sys.stderr)
    print("GATE PASS: second Helm open, page turn, and Grimmory catalog parse.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
