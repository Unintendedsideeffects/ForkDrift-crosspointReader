#!/usr/bin/env python3
"""Exercise the Claude AskUserQuestion hook against the native firmware simulator."""

from __future__ import annotations

import argparse
import configparser
import json
import os
from pathlib import Path
import selectors
import socket
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
HOOK = ROOT / "tools" / "claude-bridge" / "askquestion_hook.py"
TOKEN = "simulator-claude-smoke-token"


def pick_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def resolve_program() -> Path:
    candidates: list[Path] = []
    if build_dir := os.environ.get("PLATFORMIO_BUILD_DIR"):
        candidates.append(Path(build_dir).expanduser() / "simulator-claude" / "program")

    local_config = ROOT / "platformio.local.ini"
    if local_config.is_file():
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(local_config)
        if parser.has_option("platformio", "build_dir"):
            configured = parser.get("platformio", "build_dir")
            configured = configured.replace("${sysenv.HOME}", str(Path.home()))
            configured = configured.replace("${PROJECT_DIR}", str(ROOT))
            candidates.append(Path(configured).expanduser() / "simulator-claude" / "program")

    candidates.append(ROOT / ".pio" / "build" / "simulator-claude" / "program")
    return next((candidate for candidate in candidates if candidate.is_file()), candidates[0])


def hook_payload() -> dict:
    return {
        "session_id": "simulator-session",
        "cwd": str(ROOT),
        "tool_name": "AskUserQuestion",
        "tool_input": {
            "questions": [
                {
                    "question": "Ship this simulator-tested version?",
                    "header": "Release",
                    "multiSelect": False,
                    "options": [
                        {"label": "Ship it", "description": "Use the current implementation"},
                        {"label": "Keep working", "description": "Return to the implementation"},
                    ],
                }
            ]
        },
    }


def wait_for_ready(process: subprocess.Popen[str], timeout: float) -> tuple[list[str], str]:
    assert process.stdout is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    lines: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            break
        for _key, _mask in selector.select(timeout=0.2):
            line = process.stdout.readline()
            if not line:
                continue
            lines.append(line)
            print(line, end="")
            marker = "CLAUDE_SMOKE_READY "
            if marker in line:
                return lines, line.split(marker, 1)[1].strip()
    raise RuntimeError("simulator did not expose the Claude smoke endpoint")


def run(args: argparse.Namespace) -> int:
    if not args.no_build:
        build = subprocess.run(
            ["scripts/pio-locked.sh", "run", "-e", "simulator-claude"],
            cwd=ROOT,
            check=False,
        )
        if build.returncode != 0:
            return build.returncode

    program = resolve_program()
    if not program.is_file():
        print(f"Simulator binary not found: {program}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="forkdrift-claude-sim-") as temp_name:
        port = pick_port()
        fs_root = Path(temp_name) / "fs_"
        fs_root.mkdir()
        (fs_root / "claude_bridge.json").write_text(json.dumps({"token": TOKEN}), encoding="utf-8")

        env = os.environ.copy()
        env.update(
            {
                "CROSSPOINT_SIM_SD": str(fs_root),
                "FORKDRIFT_SIMULATOR_SMOKE_TEST": "1",
                "FORKDRIFT_SIMULATOR_SMOKE_CLAUDE_BRIDGE": "1",
                "FORKDRIFT_SIMULATOR_CLAUDE_PORT": str(port),
                "SDL_VIDEODRIVER": "dummy",
            }
        )
        if args.frames:
            frames = Path(args.frames).resolve()
            frames.mkdir(parents=True, exist_ok=True)
            env["FORKDRIFT_SIMULATOR_SMOKE_FRAMES"] = str(frames)

        process = subprocess.Popen(
            [str(program)],
            cwd=fs_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )
        output_lines: list[str] = []
        try:
            ready_lines, endpoint = wait_for_ready(process, args.timeout)
            output_lines.extend(ready_lines)

            payload = hook_payload()
            hook_env = os.environ.copy()
            hook_env.update(
                {
                    "CLAUDE_DEVICE_URL": endpoint,
                    "CLAUDE_DEVICE_TOKEN": TOKEN,
                    "CLAUDE_DEVICE_CONFIG": "/definitely/missing/claudeq.json",
                    "CLAUDE_DEVICE_POLL": "0.05",
                    "CLAUDE_DEVICE_WAIT": "10",
                }
            )
            hook = subprocess.run(
                [sys.executable, str(HOOK)],
                input=json.dumps(payload),
                text=True,
                capture_output=True,
                timeout=15,
                env=hook_env,
                check=False,
            )
            if hook.returncode != 0 or not hook.stdout:
                raise RuntimeError(f"Claude hook did not return an answer: {hook.stderr.strip()}")

            result = json.loads(hook.stdout)
            expected = {"Ship this simulator-tested version?": "Keep working"}
            actual = result["hookSpecificOutput"]["updatedInput"]["answers"]
            if actual != expected:
                raise RuntimeError(f"unexpected hook answer: {actual!r}")
            print(f"HOOK_ANSWER {json.dumps(actual, sort_keys=True)}")

            remaining, _ = process.communicate(timeout=10)
            output_lines.append(remaining)
            print(remaining, end="")
        except (KeyError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
            process.terminate()
            try:
                remaining, _ = process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                remaining, _ = process.communicate()
            output_lines.append(remaining)
            print(remaining, end="")
            print(f"Claude simulator smoke failed: {exc}", file=sys.stderr)
            return 2

        output = "".join(output_lines)
        if process.returncode != 0 or "Claude bridge simulator smoke passed" not in output:
            print(f"Simulator exited without its success marker (code {process.returncode})", file=sys.stderr)
            return 2
        print("Claude AskUserQuestion simulator round-trip passed")
        return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-build", action="store_true", help="Run an existing simulator-claude binary")
    parser.add_argument("--timeout", type=float, default=45, help="Seconds to wait for simulator readiness")
    parser.add_argument("--frames", help="Directory for inspectable PBM framebuffer captures")
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(run(parse_args()))
