#!/usr/bin/env python3
"""Prove the configured X4 completes unattended Terminus refresh cycles.

This is the hardware acceptance gate for modularity Packet 02. It verifies the
live Terminus /api/display contract, puts the device into its production
charging-screensaver path, waits through at least two timer cycles, reconnects
USB serial if necessary, and compares durable SD-backed evidence counters.

The API token is read from TERMINUS_API_TOKEN and is never printed. For USB/IP
set X4_REATTACH_COMMAND to a non-interactive command that re-imports the X4 into
the test environment. Directly attached USB needs no reattach command.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shlex
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import serial

try:
    from device_walk import DeviceLink
except ModuleNotFoundError:  # imported as scripts.verify_terminus_device by pytest
    from scripts.device_walk import DeviceLink


def fetch_display_manifest(status: dict, token: str) -> dict:
    base_url = str(status.get("base_url", "")).rstrip("/")
    device_id = str(status.get("device_id", ""))
    model = str(status.get("device_model", "xteink_x4"))
    if not base_url or not device_id:
        raise RuntimeError("Device status is missing Terminus base_url or device_id")

    request = urllib.request.Request(
        f"{base_url}/api/display",
        headers={
            "ID": device_id,
            "Access-Token": token,
            "Device-Model": model,
            "User-Agent": "ForkDrift-TRMNL-Hardware-Gate/1",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            if response.status != 200:
                raise RuntimeError(
                    f"Terminus /api/display returned HTTP {response.status}"
                )
            manifest = json.load(response)
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Terminus /api/display is unreachable: {exc}") from exc

    image_url = manifest.get("image_url")
    refresh_rate = manifest.get("refresh_rate", 900)
    try:
        refresh_seconds = max(15, min(86400, int(refresh_rate)))
    except (TypeError, ValueError) as exc:
        raise RuntimeError(
            f"Terminus returned invalid refresh_rate: {refresh_rate!r}"
        ) from exc
    if not isinstance(image_url, str) or not image_url.startswith(
        ("http://", "https://")
    ):
        raise RuntimeError("Terminus returned no usable image_url")
    return {"image_url": image_url, "refresh_seconds": refresh_seconds}


def evidence_failures(before: dict, after: dict, minimum_cycles: int) -> list[str]:
    failures: list[str] = []
    counter_names = (
        "timer_wake_count",
        "completed_cycle_count",
        "wifi_success_count",
        "fetch_success_count",
        "render_success_count",
    )
    deltas = {
        name: int(after.get(name, 0)) - int(before.get(name, 0))
        for name in counter_names
    }
    wake_delta = deltas["timer_wake_count"]
    if wake_delta < minimum_cycles:
        failures.append(
            f"only {wake_delta} timer wakes observed; need at least {minimum_cycles}"
        )
    for name in counter_names[1:]:
        if deltas[name] != wake_delta:
            failures.append(
                f"{name} advanced by {deltas[name]}, but timer wakes advanced by {wake_delta}"
            )
    rendezvous_delta = int(after.get("verification_rendezvous_count", 0)) - int(
        before.get("verification_rendezvous_count", 0)
    )
    if rendezvous_delta != 1:
        failures.append(
            f"verification rendezvous advanced by {rendezvous_delta}, expected exactly 1"
        )
    if int(after.get("verification_target_completed_cycle_count", 0)) != 0:
        failures.append("verification target was not consumed")

    required_flags = (
        "last_cycle_complete",
        "last_wifi_started",
        "last_wifi_connected",
        "last_fetch_attempted",
        "last_fetch_ok",
        "last_render_completed",
        "last_rearm_armed",
    )
    for name in required_flags:
        if after.get(name) is not True:
            failures.append(f"{name} is not true")
    if after.get("last_used_stale_image") is not False:
        failures.append("last cycle used the stale-image fallback")
    rearm_interval = int(after.get("last_rearm_interval_seconds", 0))
    server_interval = int(after.get("last_server_refresh_seconds", 0))
    if rearm_interval <= 0:
        failures.append("device recorded no positive re-arm interval")
    if server_interval <= 0:
        failures.append("device recorded no positive server interval")
    if rearm_interval != server_interval:
        failures.append(
            f"device re-arm interval {rearm_interval}s != same-cycle server interval {server_interval}s"
        )
    return failures


def run_reattach(command: str | None) -> str | None:
    if not command:
        return None
    try:
        result = subprocess.run(
            shlex.split(command),
            check=False,
            capture_output=True,
            text=True,
            timeout=45,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return str(exc)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        return f"X4 reattach command failed ({result.returncode}): {detail}"
    return None


def connect_responding_device(
    preferred_port: str | None, timeout_seconds: int, reattach_command: str | None
):
    deadline = time.monotonic() + timeout_seconds
    last_reattach = 0.0
    last_error = "no serial candidate"
    while time.monotonic() < deadline:
        now = time.monotonic()
        if reattach_command and now - last_reattach >= 20:
            reattach_error = run_reattach(reattach_command)
            if reattach_error:
                last_error = reattach_error
            last_reattach = now
        candidates = [preferred_port] if preferred_port else []
        candidates += sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
        for port in dict.fromkeys(candidate for candidate in candidates if candidate):
            try:
                link = DeviceLink(port, None)
                link.drain(0.3)
                link.ping()
                return link, port
            except (OSError, serial.SerialException, TimeoutError) as exc:
                last_error = f"{port}: {exc}"
                try:
                    link.close()
                except (UnboundLocalError, OSError, serial.SerialException):
                    pass
        time.sleep(2)
    raise RuntimeError(
        f"X4 did not return on serial within {timeout_seconds}s ({last_error})"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port", help="initial serial port (default: discover a responding X4)"
    )
    parser.add_argument(
        "--cycles",
        type=int,
        default=2,
        help="minimum unattended timer cycles (default: 2)",
    )
    parser.add_argument(
        "--arm-grace",
        type=int,
        default=120,
        help="seconds allowed for initial WiFi/fetch/render",
    )
    parser.add_argument(
        "--return-timeout",
        type=int,
        default=120,
        help="seconds allowed for USB serial to return",
    )
    parser.add_argument(
        "--unattended-timeout",
        type=int,
        default=3600,
        help="maximum seconds to poll for the completed-cycle rendezvous (default: 3600)",
    )
    parser.add_argument("--reattach-command", default=os.getenv("X4_REATTACH_COMMAND"))
    parser.add_argument(
        "--out", type=Path, default=Path("runs/terminus-full/evidence.json")
    )
    args = parser.parse_args()
    if args.cycles < 2:
        parser.error(
            "--cycles must be at least 2 so the first cycle's re-arm is proven"
        )

    token = os.getenv("TERMINUS_API_TOKEN", "")
    if not token:
        sys.exit("TERMINUS_API_TOKEN is required (it is consumed but never printed)")

    link, port = connect_responding_device(
        args.port, args.return_timeout, args.reattach_command
    )
    try:
        initial_status = link.terminus_status()
        if (
            initial_status.get("configured") is not True
            or initial_status.get("has_api_key") is not True
        ):
            raise RuntimeError("X4 does not report complete Terminus credentials")
        manifest = fetch_display_manifest(initial_status, token)
        interval = manifest["refresh_seconds"]
        before = initial_status.get("timed_refresh_evidence", {})
        print(f"Live Terminus payload valid; server cadence is {interval}s")

        link.apply_settings(
            '{"sleepScreenSplit":0,"sleepScreen":17,"timedSleepRefreshInterval":6}'
        )
        configured_status = link.terminus_status()
        if (
            configured_status.get("sleep_enabled") is not True
            or configured_status.get("timed_refresh_interval") != 6
        ):
            raise RuntimeError("X4 did not enter Terminus + Screensaver configuration")
        print(f"Baseline timer wakes: {int(before.get('timer_wake_count', 0))}")
        target = link.arm_terminus_verification(args.cycles)
        print(f"Armed awake evidence rendezvous at completed cycle {target}")
        pending_result = {
            "schema_version": 1,
            "result": "pending",
            "minimum_cycles": args.cycles,
            "initial_server_refresh_seconds": interval,
            "before": before,
            "target_completed_cycle_count": target,
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(pending_result, indent=2, sort_keys=True) + "\n")
        link.deep_sleep()
        print("Production deep sleep requested; serial may now disappear")
    finally:
        link.close()

    unattended_timeout = max(
        args.unattended_timeout, interval * args.cycles + args.arm_grace
    )
    print(
        f"Polling up to {unattended_timeout}s for the completed-cycle rendezvous",
        flush=True,
    )
    try:
        link, returned_port = connect_responding_device(
            port, unattended_timeout, args.reattach_command
        )
    except RuntimeError as exc:
        pending_result["result"] = "inconclusive"
        pending_result["collection_error"] = str(exc)
        args.out.write_text(json.dumps(pending_result, indent=2, sort_keys=True) + "\n")
        print(
            f"INCONCLUSIVE — evidence collection failed; details written to {args.out}",
            file=sys.stderr,
        )
        return 2
    try:
        final_status = link.terminus_status()
    finally:
        link.close()
    after = final_status.get("timed_refresh_evidence", {})
    failures = evidence_failures(before, after, args.cycles)

    result = {
        "schema_version": 1,
        "result": "fail" if failures else "pass",
        "minimum_cycles": args.cycles,
        "initial_server_refresh_seconds": interval,
        "returned_port": returned_port,
        "before": before,
        "after": after,
        "failures": failures,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    if failures:
        print(f"FAIL — durable evidence written to {args.out}", file=sys.stderr)
        return 1
    print(
        f"PASS — {args.cycles}+ complete Terminus cycles proven; evidence written to {args.out}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
