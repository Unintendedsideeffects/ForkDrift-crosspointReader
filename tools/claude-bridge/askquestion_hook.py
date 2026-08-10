#!/usr/bin/env python3
"""Claude Code PreToolUse hook for AskUserQuestion on a CrossPoint X4.

Claude Code starts this process only when AskUserQuestion is called. The hook
discovers a reachable CrossPoint server, sends the real question/options, waits
for the X4 button selection, and returns updatedInput.answers. There is no
standing host daemon.

Every unsupported or unavailable path exits successfully without stdout. That
is Claude Code's documented fallback contract: the normal terminal picker is
shown unchanged.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


DISCOVERY_PORT = 8134
HTTP_PORT = 80
DISCOVERY_TIMEOUT = float(os.environ.get("CLAUDE_DEVICE_DISCOVERY_TIMEOUT", "1.0"))
WAIT_SECONDS = float(os.environ.get("CLAUDE_DEVICE_WAIT", "295"))
POLL_SECONDS = float(os.environ.get("CLAUDE_DEVICE_POLL", "1.0"))
HTTP_TIMEOUT = 4.0
CONFIG_PATH = Path(
    os.environ.get("CLAUDE_DEVICE_CONFIG", "~/.config/crosspoint/claudeq.json")
).expanduser()


def load_config() -> dict:
    config: dict = {}
    try:
        loaded = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        if isinstance(loaded, dict):
            config = loaded
    except (OSError, ValueError, UnicodeDecodeError):
        pass

    if os.environ.get("CLAUDE_DEVICE_URL"):
        config["url"] = os.environ["CLAUDE_DEVICE_URL"]
    if os.environ.get("CLAUDE_DEVICE_TOKEN"):
        config["token"] = os.environ["CLAUDE_DEVICE_TOKEN"]
    return config


def discover_device() -> str | None:
    """Return the first CrossPoint HTTP URL answering the existing UDP probe."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(DISCOVERY_TIMEOUT)
        sock.bind(("", 0))
        sock.sendto(b"hello", ("255.255.255.255", DISCOVERY_PORT))
        deadline = time.monotonic() + DISCOVERY_TIMEOUT
        while time.monotonic() < deadline:
            try:
                payload, peer = sock.recvfrom(256)
            except socket.timeout:
                break
            if payload.startswith(b"crosspoint (on "):
                return f"http://{peer[0]}:{HTTP_PORT}"
    except OSError:
        return None
    finally:
        sock.close()
    return None


class DeviceClient:
    def __init__(self, base_url: str, token: str) -> None:
        self.base_url = base_url.rstrip("/")
        self.token = token

    def request(self, path: str, method: str = "GET", payload: dict | None = None):
        data = json.dumps(payload).encode("utf-8") if payload is not None else None
        headers = {"Content-Type": "application/json"} if data is not None else {}
        headers["Authorization"] = "Bearer " + self.token
        req = urllib.request.Request(
            self.base_url + path,
            data=data,
            method=method,
            headers=headers,
        )
        try:
            with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as response:
                raw = response.read()
                body = json.loads(raw) if raw else None
                return response.status, body
        except urllib.error.HTTPError as exc:
            return exc.code, None

    def cancel(self, request_id: str) -> None:
        query = urllib.parse.urlencode({"id": request_id})
        try:
            self.request(f"/api/claude/cancel?{query}", "POST", {})
        except (OSError, urllib.error.URLError, ValueError):
            pass


def supported_questions(tool_input: dict) -> list[dict] | None:
    questions = tool_input.get("questions")
    if not isinstance(questions, list) or not 1 <= len(questions) <= 4:
        return None
    for question in questions:
        if not isinstance(question, dict) or question.get("multiSelect", False):
            return None
        if not isinstance(question.get("question"), str):
            return None
        options = question.get("options")
        if not isinstance(options, list) or not 2 <= len(options) <= 4:
            return None
        if any(not isinstance(option, dict) or not isinstance(option.get("label"), str) for option in options):
            return None
    return questions


def session_payload(hook_input: dict) -> dict:
    cwd = hook_input.get("cwd") if isinstance(hook_input.get("cwd"), str) else ""
    title = os.environ.get("CLAUDEQ_TITLE")
    if not title and cwd:
        title = Path(cwd).name
    return {
        "id": hook_input.get("session_id"),
        "title": title or "Claude Code",
        "cwd": cwd,
    }


def emit_answers(questions: list[dict], answers: dict) -> None:
    json.dump(
        {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "allow",
                "updatedInput": {"questions": questions, "answers": answers},
            }
        },
        sys.stdout,
    )
    sys.stdout.write("\n")


def main() -> None:
    try:
        hook_input = json.load(sys.stdin)
    except (ValueError, UnicodeDecodeError):
        return

    if hook_input.get("tool_name") != "AskUserQuestion":
        return
    tool_input = hook_input.get("tool_input")
    if not isinstance(tool_input, dict):
        return
    questions = supported_questions(tool_input)
    if questions is None:
        return

    config = load_config()
    token = config.get("token")
    if not isinstance(token, str) or not token:
        return
    base_url = config.get("url")
    if not isinstance(base_url, str) or not base_url:
        base_url = discover_device()
    if not base_url:
        return

    client = DeviceClient(base_url, token)
    request_id: str | None = None
    delivered = False
    try:
        status, result = client.request(
            "/api/claude/ask",
            "POST",
            {"session": session_payload(hook_input), "questions": questions},
        )
        if status != 200 or not isinstance(result, dict) or not isinstance(result.get("id"), str):
            return
        request_id = result["id"]
        query = "/api/claude/answer?" + urllib.parse.urlencode({"id": request_id})
        deadline = time.monotonic() + WAIT_SECONDS

        while time.monotonic() < deadline:
            time.sleep(POLL_SECONDS)
            status, result = client.request(query)
            if status == 204:
                continue
            if status == 200 and isinstance(result, dict) and isinstance(result.get("answers"), dict):
                answers = result["answers"]
                expected = {question["question"] for question in questions}
                if set(answers) == expected and all(isinstance(value, str) for value in answers.values()):
                    emit_answers(questions, answers)
                    delivered = True
                return
            return
    except (OSError, urllib.error.URLError, ValueError):
        return
    finally:
        if request_id and not delivered:
            client.cancel(request_id)


if __name__ == "__main__":
    main()
