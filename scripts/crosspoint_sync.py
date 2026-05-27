#!/usr/bin/env python3
"""
Sync files between a local directory and a CrossPoint device over WiFi.

Endpoints used:
  - GET  /api/files
  - POST /mkdir
  - POST /upload
  - GET  /download
"""

from __future__ import annotations

import argparse
import http.client
import json
import os
import posixpath
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


class CrossPointSyncError(Exception):
    """Base exception for sync failures."""


@dataclass
class DeviceHttpError(CrossPointSyncError):
    status: int
    method: str
    target: str
    body: str

    def __str__(self) -> str:
        return f"{self.method} {self.target} failed with HTTP {self.status}: {self.body}"


@dataclass(frozen=True)
class LocalFile:
    path: Path
    size: int


@dataclass(frozen=True)
class RemoteFile:
    path: str
    size: int


def normalize_base_url(raw: str) -> str:
    value = raw.strip()
    if "://" not in value:
        value = f"http://{value}"
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme not in {"http", "https"}:
        raise CrossPointSyncError(f"Unsupported URL scheme: {parsed.scheme}")
    if not parsed.netloc:
        raise CrossPointSyncError(f"Invalid base URL: {raw}")
    return value.rstrip("/")


def normalize_remote_path(path: str) -> str:
    value = path.strip() if path else "/"
    normalized = posixpath.normpath("/" + value.lstrip("/"))
    if normalized == "/.":
        return "/"
    return normalized


def join_remote_path(base: str, name: str) -> str:
    parent = normalize_remote_path(base)
    if parent == "/":
        return normalize_remote_path("/" + name)
    return normalize_remote_path(parent + "/" + name)


def relative_remote_path(path: str, root: str) -> str:
    path_posix = PurePosixPath(normalize_remote_path(path))
    root_posix = PurePosixPath(normalize_remote_path(root))
    rel = path_posix.relative_to(root_posix)
    rel_text = rel.as_posix()
    return "" if rel_text == "." else rel_text


class CrossPointClient:
    def __init__(self, base_url: str, timeout: float) -> None:
        normalized = normalize_base_url(base_url)
        parsed = urllib.parse.urlparse(normalized)
        self.timeout = timeout
        self._parsed = parsed
        self._base_path = parsed.path.rstrip("/")
        self._host = parsed.hostname or ""
        self._port = parsed.port or (443 if parsed.scheme == "https" else 80)

    def _url(self, endpoint: str, params: dict[str, str] | None = None) -> str:
        if not endpoint.startswith("/"):
            raise ValueError("Endpoint must start with '/'")
        path = f"{self._base_path}{endpoint}"
        query = urllib.parse.urlencode(params or {}, quote_via=urllib.parse.quote)
        return urllib.parse.urlunparse((self._parsed.scheme, self._parsed.netloc, path, "", query, ""))

    def _target(self, endpoint: str, params: dict[str, str] | None = None) -> str:
        path = f"{self._base_path}{endpoint}"
        query = urllib.parse.urlencode(params or {}, quote_via=urllib.parse.quote)
        return f"{path}?{query}" if query else path

    def _request(
        self,
        method: str,
        endpoint: str,
        params: dict[str, str] | None = None,
        data: bytes | None = None,
        headers: dict[str, str] | None = None,
    ) -> tuple[int, bytes]:
        url = self._url(endpoint, params)
        request = urllib.request.Request(url=url, data=data, method=method, headers=headers or {})
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as err:
            body = err.read().decode("utf-8", errors="replace")
            raise DeviceHttpError(err.code, method, url, body) from err
        except urllib.error.URLError as err:
            raise CrossPointSyncError(f"{method} {url} failed: {err.reason}") from err

    def healthcheck(self) -> None:
        self._request("GET", "/api/status")

    def list_files(self, path: str) -> list[dict]:
        normalized = normalize_remote_path(path)
        _, body = self._request("GET", "/api/files", params={"path": normalized})
        try:
            parsed = json.loads(body.decode("utf-8"))
        except json.JSONDecodeError as err:
            raise CrossPointSyncError(f"Invalid JSON from /api/files for '{normalized}'") from err
        if not isinstance(parsed, list):
            raise CrossPointSyncError(f"Unexpected /api/files response type for '{normalized}'")
        return parsed

    def create_folder(self, parent_path: str, folder_name: str) -> None:
        normalized_parent = normalize_remote_path(parent_path)
        payload = json.dumps({"name": folder_name, "path": normalized_parent}).encode("utf-8")
        try:
            self._request(
                "POST",
                "/mkdir",
                data=payload,
                headers={"Content-Type": "application/json"},
            )
        except DeviceHttpError as err:
            if err.status == 400 and err.body.strip() == "Folder already exists":
                return
            raise

    def _new_connection(self) -> http.client.HTTPConnection:
        if self._parsed.scheme == "https":
            context = ssl.create_default_context()
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            return http.client.HTTPSConnection(self._host, self._port, timeout=self.timeout, context=context)
        return http.client.HTTPConnection(self._host, self._port, timeout=self.timeout)

    def upload_file(self, local_path: Path, remote_dir: str) -> None:
        normalized_dir = normalize_remote_path(remote_dir)
        boundary = f"----CrossPointBoundary{uuid.uuid4().hex}"
        safe_name = local_path.name.replace('"', "_")

        prefix = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="{safe_name}"\r\n'
            "Content-Type: application/octet-stream\r\n"
            "\r\n"
        ).encode("utf-8")
        suffix = f"\r\n--{boundary}--\r\n".encode("utf-8")
        content_length = len(prefix) + local_path.stat().st_size + len(suffix)
        target = self._target("/upload", params={"path": normalized_dir})

        connection = self._new_connection()
        try:
            connection.putrequest("POST", target)
            connection.putheader("Host", self._parsed.netloc)
            connection.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
            connection.putheader("Content-Length", str(content_length))
            connection.putheader("Connection", "close")
            connection.endheaders()

            connection.send(prefix)
            with local_path.open("rb") as handle:
                while True:
                    chunk = handle.read(64 * 1024)
                    if not chunk:
                        break
                    connection.send(chunk)
            connection.send(suffix)

            response = connection.getresponse()
            body = response.read().decode("utf-8", errors="replace")
            if response.status != 200:
                raise DeviceHttpError(response.status, "POST", target, body)
        finally:
            connection.close()

    def download_file(self, remote_path: str, local_path: Path) -> None:
        normalized_path = normalize_remote_path(remote_path)
        url = self._url("/download", params={"path": normalized_path})
        tmp_path = local_path.with_name(local_path.name + ".part")
        local_path.parent.mkdir(parents=True, exist_ok=True)

        try:
            request = urllib.request.Request(url=url, method="GET")
            with urllib.request.urlopen(request, timeout=self.timeout) as response, tmp_path.open("wb") as handle:
                while True:
                    chunk = response.read(64 * 1024)
                    if not chunk:
                        break
                    handle.write(chunk)
            os.replace(tmp_path, local_path)
        except urllib.error.HTTPError as err:
            body = err.read().decode("utf-8", errors="replace")
            raise DeviceHttpError(err.code, "GET", url, body) from err
        except urllib.error.URLError as err:
            raise CrossPointSyncError(f"GET {url} failed: {err.reason}") from err
        finally:
            if tmp_path.exists():
                tmp_path.unlink(missing_ok=True)


def build_local_index(local_root: Path, include_hidden: bool) -> dict[str, LocalFile]:
    if not local_root.exists():
        raise CrossPointSyncError(f"Local root does not exist: {local_root}")
    if not local_root.is_dir():
        raise CrossPointSyncError(f"Local root is not a directory: {local_root}")

    index: dict[str, LocalFile] = {}
    for current_root, dirnames, filenames in os.walk(local_root):
        if not include_hidden:
            dirnames[:] = [name for name in dirnames if not name.startswith(".")]
            filenames = [name for name in filenames if not name.startswith(".")]

        root_path = Path(current_root)
        for filename in filenames:
            path = root_path / filename
            if not path.is_file():
                continue
            rel = path.relative_to(local_root).as_posix()
            index[rel] = LocalFile(path=path, size=path.stat().st_size)
    return index


def build_remote_index(client: CrossPointClient, remote_root: str) -> dict[str, RemoteFile]:
    root = normalize_remote_path(remote_root)
    index: dict[str, RemoteFile] = {}
    queue = [root]
    seen: set[str] = set()

    while queue:
        current = queue.pop()
        if current in seen:
            continue
        seen.add(current)

        entries = client.list_files(current)
        for entry in entries:
            name = str(entry.get("name", ""))
            if not name:
                continue
            child = join_remote_path(current, name)
            is_directory = bool(entry.get("isDirectory", False))

            if is_directory:
                queue.append(child)
                continue

            rel = relative_remote_path(child, root)
            if not rel:
                continue
            size = int(entry.get("size", 0))
            index[rel] = RemoteFile(path=child, size=size)

    return index


def ensure_remote_dir(client: CrossPointClient, remote_dir: str, cache: set[str], dry_run: bool = False) -> None:
    target = normalize_remote_path(remote_dir)
    if target in cache:
        return
    if target == "/":
        cache.add("/")
        return

    parent = normalize_remote_path(posixpath.dirname(target) or "/")
    ensure_remote_dir(client, parent, cache, dry_run=dry_run)

    folder_name = posixpath.basename(target)
    if folder_name and not dry_run:
        client.create_folder(parent, folder_name)
    cache.add(target)


def print_transfer(action: str, rel_path: str, size: int, reason: str) -> None:
    print(f"{action:>6}  {size:>10} bytes  {rel_path}  [{reason}]")


def command_scan(args: argparse.Namespace) -> int:
    client = CrossPointClient(args.base_url, args.timeout)
    client.healthcheck()
    remote_root = normalize_remote_path(args.remote_root)
    remote = build_remote_index(client, remote_root)

    print(f"Remote root: {remote_root}")
    print(f"Files: {len(remote)}")
    total = sum(item.size for item in remote.values())
    print(f"Total bytes: {total}")
    for rel_path, item in sorted(remote.items()):
        print(f"{item.size:>10}  {rel_path}")
    return 0


def command_push(args: argparse.Namespace) -> int:
    client = CrossPointClient(args.base_url, args.timeout)
    client.healthcheck()

    local_root = Path(args.local_root).resolve()
    remote_root = normalize_remote_path(args.remote_root)
    local = build_local_index(local_root, include_hidden=args.include_hidden)
    remote = {} if args.force else build_remote_index(client, remote_root)

    ensure_cache = {"/"}
    ensure_remote_dir(client, remote_root, ensure_cache, dry_run=args.dry_run)

    uploaded = 0
    skipped = 0

    for rel_path, local_file in sorted(local.items()):
        remote_file = remote.get(rel_path)
        if not args.force and remote_file and remote_file.size == local_file.size:
            skipped += 1
            continue

        remote_path = join_remote_path(remote_root, rel_path)
        remote_dir = normalize_remote_path(posixpath.dirname(remote_path) or "/")
        ensure_remote_dir(client, remote_dir, ensure_cache, dry_run=args.dry_run)
        reason = "force" if args.force else ("new" if remote_file is None else "size-changed")
        print_transfer("push", rel_path, local_file.size, reason)
        if not args.dry_run:
            client.upload_file(local_file.path, remote_dir)
        uploaded += 1

    print(f"\nPush complete: uploaded={uploaded}, skipped={skipped}, dry_run={args.dry_run}")
    return 0


def command_pull(args: argparse.Namespace) -> int:
    client = CrossPointClient(args.base_url, args.timeout)
    client.healthcheck()

    local_root = Path(args.local_root).resolve()
    local_root.mkdir(parents=True, exist_ok=True)

    remote_root = normalize_remote_path(args.remote_root)
    remote = build_remote_index(client, remote_root)
    local = {} if args.force else build_local_index(local_root, include_hidden=True)

    downloaded = 0
    skipped = 0

    for rel_path, remote_file in sorted(remote.items()):
        local_file = local.get(rel_path)
        if not args.force and local_file and local_file.size == remote_file.size:
            skipped += 1
            continue

        target_path = local_root / Path(rel_path)
        reason = "force" if args.force else ("new" if local_file is None else "size-changed")
        print_transfer("pull", rel_path, remote_file.size, reason)
        if not args.dry_run:
            client.download_file(remote_file.path, target_path)
        downloaded += 1

    print(f"\nPull complete: downloaded={downloaded}, skipped={skipped}, dry_run={args.dry_run}")
    return 0


def command_sync(args: argparse.Namespace) -> int:
    client = CrossPointClient(args.base_url, args.timeout)
    client.healthcheck()

    local_root = Path(args.local_root).resolve()
    local_root.mkdir(parents=True, exist_ok=True)

    remote_root = normalize_remote_path(args.remote_root)
    local = build_local_index(local_root, include_hidden=args.include_hidden)
    remote = build_remote_index(client, remote_root)

    local_paths = set(local.keys())
    remote_paths = set(remote.keys())
    shared = local_paths & remote_paths
    local_only = local_paths - remote_paths
    remote_only = remote_paths - local_paths

    push_ops: list[tuple[str, str]] = []
    pull_ops: list[tuple[str, str]] = []

    for rel_path in sorted(local_only):
        push_ops.append((rel_path, "missing-on-device"))
    for rel_path in sorted(remote_only):
        pull_ops.append((rel_path, "missing-local"))
    for rel_path in sorted(shared):
        if local[rel_path].size == remote[rel_path].size:
            continue
        if args.prefer == "local":
            push_ops.append((rel_path, "size-conflict-prefer-local"))
        else:
            pull_ops.append((rel_path, "size-conflict-prefer-remote"))

    ensure_cache = {"/"}
    ensure_remote_dir(client, remote_root, ensure_cache, dry_run=args.dry_run)

    pushed = 0
    pulled = 0
    skipped = len(shared) - sum(1 for rel_path in shared if local[rel_path].size != remote[rel_path].size)

    for rel_path, reason in push_ops:
        local_file = local[rel_path]
        remote_path = join_remote_path(remote_root, rel_path)
        remote_dir = normalize_remote_path(posixpath.dirname(remote_path) or "/")
        ensure_remote_dir(client, remote_dir, ensure_cache, dry_run=args.dry_run)
        print_transfer("push", rel_path, local_file.size, reason)
        if not args.dry_run:
            client.upload_file(local_file.path, remote_dir)
        pushed += 1

    for rel_path, reason in pull_ops:
        remote_file = remote[rel_path]
        target_path = local_root / Path(rel_path)
        print_transfer("pull", rel_path, remote_file.size, reason)
        if not args.dry_run:
            client.download_file(remote_file.path, target_path)
        pulled += 1

    print(
        "\nSync complete: "
        f"pushed={pushed}, pulled={pulled}, unchanged={skipped}, "
        f"prefer={args.prefer}, dry_run={args.dry_run}"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Sync files with a CrossPoint device over WiFi.")
    parser.add_argument(
        "--base-url",
        default="http://crosspoint.local",
        help="Device base URL (default: http://crosspoint.local)",
    )
    parser.add_argument("--timeout", type=float, default=30.0, help="HTTP timeout in seconds (default: 30)")

    subparsers = parser.add_subparsers(dest="command", required=True)

    scan = subparsers.add_parser("scan", help="List files on device recursively")
    scan.add_argument("--remote-root", default="/", help="Remote path to scan (default: /)")
    scan.set_defaults(handler=command_scan)

    push = subparsers.add_parser("push", help="Upload local files to device")
    push.add_argument("local_root", help="Local directory to upload")
    push.add_argument("--remote-root", default="/", help="Remote destination root (default: /)")
    push.add_argument("--include-hidden", action="store_true", help="Include hidden local files and directories")
    push.add_argument("--force", action="store_true", help="Upload all files, even when sizes match")
    push.add_argument("--dry-run", action="store_true", help="Show planned actions without uploading")
    push.set_defaults(handler=command_push)

    pull = subparsers.add_parser("pull", help="Download device files to local directory")
    pull.add_argument("local_root", help="Local destination directory")
    pull.add_argument("--remote-root", default="/", help="Remote source root (default: /)")
    pull.add_argument("--force", action="store_true", help="Download all files, even when sizes match")
    pull.add_argument("--dry-run", action="store_true", help="Show planned actions without downloading")
    pull.set_defaults(handler=command_pull)

    sync = subparsers.add_parser("sync", help="Bidirectional sync using file-size comparison")
    sync.add_argument("local_root", help="Local directory for sync")
    sync.add_argument("--remote-root", default="/", help="Remote sync root (default: /)")
    sync.add_argument("--include-hidden", action="store_true", help="Include hidden local files and directories")
    sync.add_argument(
        "--prefer",
        choices=["local", "remote"],
        default="local",
        help="Conflict policy when same path has different sizes (default: local)",
    )
    sync.add_argument("--dry-run", action="store_true", help="Show planned actions without changing files")
    sync.set_defaults(handler=command_sync)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except KeyboardInterrupt:
        print("\nInterrupted", file=sys.stderr)
        return 130
    except CrossPointSyncError as err:
        print(f"ERROR: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
