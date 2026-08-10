#!/usr/bin/env python3

from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading
import unittest


HOOK = Path(__file__).with_name("askquestion_hook.py")


class MockState:
    def __init__(self) -> None:
        self.ask_body = None
        self.answer_statuses = []
        self.cancelled = False


class MockHandler(BaseHTTPRequestHandler):
    state: MockState

    def log_message(self, *_args) -> None:
        pass

    def _json(self, status: int, body: dict | None = None) -> None:
        encoded = json.dumps(body).encode() if body is not None else b""
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        if self.path == "/api/claude/ask":
            self.state.ask_body = body
            self._json(200, {"id": "q-test"})
            return
        if self.path.startswith("/api/claude/cancel?"):
            self.state.cancelled = True
            self._json(200, {"status": "ok"})
            return
        self._json(404)

    def do_GET(self) -> None:
        if self.path.startswith("/api/claude/answer?"):
            status, body = self.state.answer_statuses.pop(0)
            self._json(status, body)
            return
        self._json(404)


class HookContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.state = MockState()
        handler = type("BoundMockHandler", (MockHandler,), {"state": self.state})
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def run_hook(self, payload: dict, *, url: str | None = None) -> subprocess.CompletedProcess:
        endpoint = url or f"http://127.0.0.1:{self.server.server_port}"
        env = {
            "CLAUDE_DEVICE_URL": endpoint,
            "CLAUDE_DEVICE_TOKEN": "test-token",
            "CLAUDE_DEVICE_CONFIG": "/definitely/missing/claudeq.json",
            "CLAUDE_DEVICE_POLL": "0.01",
            "CLAUDE_DEVICE_WAIT": "0.2",
        }
        return subprocess.run(
            [sys.executable, str(HOOK)],
            input=json.dumps(payload),
            text=True,
            capture_output=True,
            timeout=2,
            env=env,
            check=False,
        )

    @staticmethod
    def question_payload(*, multi_select: bool = False) -> dict:
        return {
            "session_id": "session-1",
            "cwd": "/work/project-one",
            "tool_name": "AskUserQuestion",
            "tool_input": {
                "questions": [
                    {
                        "question": "Ship this version?",
                        "header": "Release",
                        "multiSelect": multi_select,
                        "options": [
                            {"label": "Yes", "description": "Publish it"},
                            {"label": "No", "description": "Keep working"},
                        ],
                    }
                ]
            },
        }

    def test_returns_claude_updated_input_answers(self) -> None:
        self.state.answer_statuses = [
            (204, None),
            (200, {"answers": {"Ship this version?": "Yes"}}),
        ]
        payload = self.question_payload()

        result = self.run_hook(payload)

        self.assertEqual(result.returncode, 0, result.stderr)
        output = json.loads(result.stdout)
        hook_output = output["hookSpecificOutput"]
        self.assertEqual(hook_output["hookEventName"], "PreToolUse")
        self.assertEqual(hook_output["permissionDecision"], "allow")
        self.assertEqual(hook_output["updatedInput"]["questions"], payload["tool_input"]["questions"])
        self.assertEqual(hook_output["updatedInput"]["answers"], {"Ship this version?": "Yes"})
        self.assertEqual(self.state.ask_body["session"]["title"], "project-one")
        self.assertFalse(self.state.cancelled)

    def test_unsupported_multiselect_falls_back_without_contacting_device(self) -> None:
        result = self.run_hook(self.question_payload(multi_select=True))

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIsNone(self.state.ask_body)

    def test_dropped_question_falls_back_and_cancels(self) -> None:
        self.state.answer_statuses = [(409, None)]

        result = self.run_hook(self.question_payload())

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertTrue(self.state.cancelled)

    def test_unreachable_device_falls_back_silently(self) -> None:
        result = self.run_hook(self.question_payload(), url="http://127.0.0.1:9")

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_other_tools_are_ignored(self) -> None:
        payload = self.question_payload()
        payload["tool_name"] = "Bash"

        result = self.run_hook(payload)

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIsNone(self.state.ask_body)


if __name__ == "__main__":
    unittest.main()
