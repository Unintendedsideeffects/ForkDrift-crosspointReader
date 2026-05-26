#!/usr/bin/env python3
"""
Executable contract checks for the fork-drift consumer HTTP surface.

This validates the stable response/request shapes published for consumers by
exercising scripts/contract_server.py directly.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER_SCRIPT = ROOT / "scripts" / "contract_server.py"
OPENAPI_SPEC = ROOT / "docs" / "http-api.openapi.yaml"


class HttpResponse:
    def __init__(self, status: int, body: bytes, content_type: str):
        self.status = status
        self.body = body
        self.content_type = content_type

    @property
    def text(self) -> str:
        return self.body.decode("utf-8")

    def json(self):
        return json.loads(self.text)


def request(base_url: str, method: str, path: str, body: bytes = b"", content_type: str | None = None) -> HttpResponse:
    req = urllib.request.Request(f"{base_url}{path}", data=body if method != "GET" else None, method=method)
    if content_type is not None:
        req.add_header("Content-Type", content_type)

    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return HttpResponse(resp.status, resp.read(), resp.headers.get("Content-Type", ""))
    except urllib.error.HTTPError as err:
        return HttpResponse(err.code, err.read(), err.headers.get("Content-Type", ""))
    except urllib.error.URLError:
        return HttpResponse(0, b"", "")


def get_json(base_url: str, path: str):
    resp = request(base_url, "GET", path)
    expect(resp.status == 200, f"{path} should return 200, got {resp.status}")
    expect("application/json" in resp.content_type, f"{path} should return JSON, got {resp.content_type!r}")
    return resp.json()


def post_json(base_url: str, path: str, payload) -> HttpResponse:
    return request(
        base_url,
        "POST",
        path,
        json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        "application/json",
    )


def post_form(base_url: str, path: str, payload: dict[str, str]) -> HttpResponse:
    return request(
        base_url,
        "POST",
        path,
        urllib.parse.urlencode(payload).encode("utf-8"),
        "application/x-www-form-urlencoded",
    )


def post_multipart(base_url: str, path: str, file_name: str, file_bytes: bytes) -> HttpResponse:
    boundary = "forkdrift-contract-boundary"
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{file_name}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8") + file_bytes + f"\r\n--{boundary}--\r\n".encode("utf-8")
    return request(base_url, "POST", path, body, f"multipart/form-data; boundary={boundary}")


def expect(condition: bool, message: str):
    if not condition:
        raise AssertionError(message)


def wait_until_ready(base_url: str):
    deadline = time.time() + 20
    while time.time() < deadline:
        resp = request(base_url, "GET", "/_test/ping")
        if resp.status == 200 and resp.text == "pong":
            return
        time.sleep(0.25)
    raise RuntimeError("contract_server.py did not become ready in time")


def seed(base_url: str, payload):
    resp = post_json(base_url, "/_test/seed", payload)
    expect(resp.status == 200, f"/_test/seed should return 200, got {resp.status}")


def reset(base_url: str):
    resp = post_json(base_url, "/_test/reset", {})
    expect(resp.status == 200, f"/_test/reset should return 200, got {resp.status}")


def assert_spec_markers():
    spec = OPENAPI_SPEC.read_text(encoding="utf-8")
    for marker in (
        "version: 1.0.0",
        "/api/screenshot:",
        "operationId: triggerScreenshot",
        "VisibleWhen:",
        "visibleWhen:",
        "required: [name, size, isDirectory]",
        "modified:",
        "Optional EPUB hint.",
        "Accepts a JSON body with `from` and `to`.",
    ):
        expect(marker in spec, f"OpenAPI spec is missing marker: {marker}")


def run_checks(base_url: str):
    assert_spec_markers()
    reset(base_url)

    status = get_json(base_url, "/api/status")
    for key in (
        "version",
        "protocolVersion",
        "wifiStatus",
        "ip",
        "mode",
        "rssi",
        "freeHeap",
        "uptime",
        "openBook",
        "otaSelectedBundle",
        "otaInstalledBundle",
        "otaInstalledFeatures",
    ):
        expect(key in status, f"/api/status missing key: {key}")

    seed(
        base_url,
        {
            "files": {
                "/library": [
                    {"name": "books", "size": 0, "isDirectory": True, "modified": 10, "isEpub": False},
                    {"name": "dune.epub", "size": 1234, "isDirectory": False, "modified": 20, "isEpub": True},
                ]
            }
        },
    )
    files = get_json(base_url, "/api/files?path=/library")
    expect(isinstance(files, list) and len(files) == 2, "/api/files should return the seeded file list")
    expect(files[1]["name"] == "dune.epub", "/api/files should preserve entry names")
    expect(files[1]["isDirectory"] is False, "/api/files should preserve isDirectory")
    expect(files[1]["modified"] == 20, "/api/files should preserve optional modified timestamps")
    expect(files[1]["isEpub"] is True, "/api/files should preserve optional isEpub hints")

    legacy_rename = post_form(base_url, "/rename", {"path": "/library/dune.epub", "name": "dune-renamed.epub"})
    expect(legacy_rename.status == 400, f"/rename should reject legacy form fields, got {legacy_rename.status}")
    expect(legacy_rename.text == "Use JSON from/to body", "/rename should explain the JSON-only contract")
    rename = post_json(base_url, "/rename", {"from": "/library/dune.epub", "to": "dune-renamed.epub"})
    expect(rename.status == 200, f"/rename should accept JSON from/to, got {rename.status}")

    legacy_move = post_form(base_url, "/move", {"path": "/library/dune-renamed.epub", "dest": "/archive"})
    expect(legacy_move.status == 400, f"/move should reject legacy form fields, got {legacy_move.status}")
    expect(legacy_move.text == "Use JSON from/to body", "/move should explain the JSON-only contract")
    move = post_json(base_url, "/move", {"from": "/library/dune-renamed.epub", "to": "/archive"})
    expect(move.status == 200, f"/move should accept JSON from/to, got {move.status}")

    mutations = get_json(base_url, "/_test/mutations")
    expect(mutations["renames"][-1] == ["/library/dune.epub", "dune-renamed.epub"], "/rename should record JSON mutation")
    expect(mutations["moves"][-1] == ["/library/dune-renamed.epub", "/archive"], "/move should record JSON mutation")

    seed(
        base_url,
        {
            "settings": [
                {
                    "key": "sleepScreen",
                    "name": "Sleep screen",
                    "category": "Display",
                    "type": "enum",
                    "value": 3,
                    "options": ["Default", "Book cover"],
                    "hasValue": True,
                    "visibleWhen": {"key": "theme", "eq": 1},
                }
            ]
        },
    )
    settings = get_json(base_url, "/api/settings")
    expect(settings[0]["name"] == "Sleep screen", "/api/settings should expose setting names")
    expect(settings[0]["visibleWhen"]["key"] == "theme", "/api/settings should expose visibleWhen metadata")

    seed(
        base_url,
        {
            "recentBooks": [
                {
                    "path": "/books/dune.epub",
                    "title": "Dune",
                    "author": "Frank Herbert",
                    "last_position": "Ch 3 27%",
                    "last_opened": 0,
                    "hasCover": True,
                    "progress": {
                        "format": "epub",
                        "percent": 27.32,
                        "page": 4,
                        "pageCount": 12,
                        "position": "Ch 3 27%",
                        "spineIndex": 2,
                    },
                }
            ]
        },
    )
    recent = get_json(base_url, "/api/recent")
    expect(recent[0]["hasCover"] is True, "/api/recent should expose hasCover")
    expect(recent[0]["progress"]["format"] == "epub", "/api/recent should expose nested progress")

    seed(
        base_url,
        {
            "wifiNetworks": [{"ssid": "HomeNet", "rssi": -42, "secured": True, "connected": True}],
            "status": {"version": "1.2.3", "wifiStatus": "Connected", "mode": "STA", "ip": "192.168.1.20", "rssi": -42},
            "otaStatus": {
                "status": "done",
                "available": True,
                "latestVersion": "1.2.4",
                "errorCode": 0,
                "message": "",
            },
            "remoteKeyboardSession": {
                "active": True,
                "id": 42,
                "title": "WiFi Password",
                "text": "draft",
                "maxLength": 64,
                "isPassword": True,
                "claimedBy": "android",
            },
            "userFontScan": {"families": 2, "activeLoaded": True},
        },
    )
    wifi_scan = get_json(base_url, "/api/wifi/scan")
    expect(wifi_scan[0]["secured"] is True, "/api/wifi/scan should expose secured flag")
    expect("encrypted" not in wifi_scan[0], "/api/wifi/scan should not expose legacy encrypted alias")
    expect(wifi_scan[0]["saved"] is False, "/api/wifi/scan should expose saved flag")
    wifi_status = get_json(base_url, "/api/wifi/status")
    expect(wifi_status["connected"] is True and wifi_status["mode"] == "STA", "/api/wifi/status should expose connected STA state")
    ota = get_json(base_url, "/api/ota/check")
    expect(ota["currentVersion"] == "1.2.3", "/api/ota/check should include currentVersion")
    expect(ota["latestVersion"] == "1.2.4", "/api/ota/check should include latestVersion")
    expect("latest_version" not in ota, "/api/ota/check should not expose legacy latest_version")
    expect("error_code" not in ota, "/api/ota/check should not expose legacy error_code")
    remote_keyboard = get_json(base_url, "/api/remote-keyboard/session")
    expect(remote_keyboard["active"] is True and remote_keyboard["id"] == 42, "/api/remote-keyboard/session should expose session snapshot")

    todo = post_form(base_url, "/api/todo/entry", {"type": "todo", "text": "Buy milk"})
    expect(todo.status == 200, f"/api/todo/entry should return 200, got {todo.status}")
    expect("application/json" in todo.content_type, f"/api/todo/entry should return JSON, got {todo.content_type!r}")
    expect(todo.json() == {"ok": True}, "/api/todo/entry should return {'ok': true}")

    todo_alias = post_json(
        base_url,
        "/api/todo/today",
        {"items": [{"text": "Agenda", "type": "agenda", "checked": False, "is_header": True}]},
    )
    expect(todo_alias.status == 400, f"/api/todo/today should reject is_header alias, got {todo_alias.status}")
    expect(todo_alias.text == "Use isHeader", "/api/todo/today should explain the canonical isHeader field")

    open_book = post_json(base_url, "/api/open-book", {"path": "/books/dune.epub"})
    expect(open_book.status == 202, f"/api/open-book should return 202, got {open_book.status}")
    expect(open_book.json() == {"status": "opening"}, "/api/open-book should return opening status")

    remote_button = post_json(base_url, "/api/remote/button", {"button": "page_forward"})
    expect(remote_button.status == 202, f"/api/remote/button should return 202, got {remote_button.status}")
    expect(remote_button.json() == {"status": "ok"}, "/api/remote/button should return ok status")

    screenshot = post_json(base_url, "/api/screenshot", {})
    expect(screenshot.status == 202, f"/api/screenshot should return 202, got {screenshot.status}")
    expect(screenshot.json() == {"status": "ok"}, "/api/screenshot should return ok status")

    font_rescan = request(base_url, "POST", "/api/user-fonts/rescan")
    expect(font_rescan.status == 200, f"/api/user-fonts/rescan should return 200, got {font_rescan.status}")
    expect("application/json" in font_rescan.content_type, f"/api/user-fonts/rescan should return JSON, got {font_rescan.content_type!r}")
    expect(font_rescan.json() == {"families": 2, "activeLoaded": True}, "/api/user-fonts/rescan should return seeded scan metadata")

    font_upload = post_multipart(base_url, "/api/user-fonts/upload", "demo.cpf", b"font")
    expect(font_upload.status == 200, f"/api/user-fonts/upload should return 200, got {font_upload.status}")
    expect("application/json" in font_upload.content_type, f"/api/user-fonts/upload should return JSON, got {font_upload.content_type!r}")
    expect(font_upload.json() == {"ok": True, "families": 2, "activeLoaded": True}, "/api/user-fonts/upload should return seeded upload metadata")


def main():
    parser = argparse.ArgumentParser(description="Validate contract_server.py against the published consumer HTTP contract.")
    parser.add_argument("--port", type=int, default=8876, help="Port to use for the temporary contract server")
    args = parser.parse_args()

    base_url = f"http://127.0.0.1:{args.port}"
    proc = subprocess.Popen(
        [sys.executable, str(SERVER_SCRIPT), "--port", str(args.port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    try:
        wait_until_ready(base_url)
        run_checks(base_url)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)

    print("Contract server validation passed.")


if __name__ == "__main__":
    main()
