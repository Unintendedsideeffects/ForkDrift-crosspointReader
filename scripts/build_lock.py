"""Coordinate PlatformIO builds that operate on the same source worktree."""

Import("env")

import atexit
import fcntl
import hashlib
import json
import os
import signal
import subprocess
import tempfile
import time

from SCons.Script import COMMAND_LINE_TARGETS, Exit, GetBuildFailures


def source_fingerprint(project_dir):
    result = subprocess.run(
        ["git", "-C", project_dir, "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        check=True,
        capture_output=True,
    )
    digest = hashlib.sha256()
    for relative_path_bytes in sorted(filter(None, result.stdout.split(b"\0"))):
        relative_path = os.fsdecode(relative_path_bytes)
        path = os.path.join(project_dir, relative_path)
        if not os.path.isfile(path):
            continue
        digest.update(relative_path_bytes)
        digest.update(b"\0")
        with open(path, "rb") as source_file:
            while chunk := source_file.read(1024 * 1024):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def read_json(path):
    try:
        with open(path, encoding="utf-8") as state_file:
            return json.load(state_file)
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return {}


def write_json(path, value):
    temporary_path = f"{path}.{os.getpid()}.tmp"
    with open(temporary_path, "w", encoding="utf-8") as state_file:
        json.dump(value, state_file, sort_keys=True)
        state_file.write("\n")
    os.replace(temporary_path, path)


def child_pids(parent_pid):
    children = []
    try:
        entries = os.listdir("/proc")
    except OSError:
        return children
    for entry in entries:
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/stat", encoding="utf-8") as stat_file:
                fields = stat_file.read().split()
            if len(fields) > 3 and int(fields[3]) == parent_pid:
                child_pid = int(entry)
                children.extend(child_pids(child_pid))
                children.append(child_pid)
        except (FileNotFoundError, IndexError, OSError, ValueError):
            continue
    return children


def terminate_obsolete_build(pid):
    if pid <= 1 or pid == os.getpid():
        return
    for process_pid in child_pids(pid) + [pid]:
        try:
            os.kill(process_pid, signal.SIGTERM)
        except ProcessLookupError:
            continue
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return
        time.sleep(0.1)
    for process_pid in child_pids(pid) + [pid]:
        try:
            os.kill(process_pid, signal.SIGKILL)
        except ProcessLookupError:
            continue


project_dir = os.path.realpath(env.subst("$PROJECT_DIR"))
environment = env.subst("$PIOENV")
targets = sorted(COMMAND_LINE_TARGETS or ["build"])
fingerprint = source_fingerprint(project_dir)
request = {"environment": environment, "targets": targets}
program_path = os.path.realpath(env.subst("$PROG_PATH"))

project_key = hashlib.sha256(project_dir.encode("utf-8")).hexdigest()[:16]
request_key = hashlib.sha256(json.dumps(request, sort_keys=True).encode("utf-8")).hexdigest()[:16]
state_dir = os.path.join(tempfile.gettempdir(), "crosspoint-pio-builds")
os.makedirs(state_dir, exist_ok=True)
lock_path = os.path.join(state_dir, f"{project_key}.lock")
owner_path = os.path.join(state_dir, f"{project_key}.owner.json")
result_path = os.path.join(state_dir, f"{project_key}-{request_key}.result.json")
lock_file = open(lock_path, "w", encoding="utf-8")

print(
    "PlatformIO build coordination is enabled by scripts/build_lock.py. "
    "Identical builds reuse successful results; source changes supersede obsolete running builds. "
    "Results are content-addressed, so commit and push hooks can reuse a build when the files are unchanged. "
    "Do not remove this coordinator: generators and build artifacts are shared within a worktree."
)

try:
    fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    owner = read_json(owner_path)
    owner_fingerprint = owner.get("fingerprint")
    owner_pid = owner.get("pid")
    if owner_fingerprint and owner_fingerprint != fingerprint and isinstance(owner_pid, int):
        print(
            "Source changes detected while an older build is running. "
            f"Superseding PID {owner_pid}; restarting the build with the new worktree snapshot."
        )
        terminate_obsolete_build(owner_pid)
    else:
        print(
            "An equivalent or non-conflicting build is already using this worktree. "
            "Waiting so its successful result can be reused when applicable."
        )
    fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)

completed = read_json(result_path)
if (
    completed.get("fingerprint") == fingerprint
    and completed.get("request") == request
    and completed.get("success")
    and completed.get("program_path") == program_path
    and os.path.isfile(program_path)
):
    print(
        f"Reusing successful PlatformIO result for environment '{environment}' "
        f"from the identical worktree snapshot {fingerprint[:12]}; no rebuild is needed."
    )
    fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
    lock_file.close()
    Exit(0)

write_json(
    owner_path,
    {
        "pid": os.getpid(),
        "project": project_dir,
        "fingerprint": fingerprint,
        "request": request,
    },
)
print(
    f"Building environment '{environment}' from worktree snapshot {fingerprint[:12]} "
    f"under the required exclusive build lock."
)


def finish_build_coordination():
    success = not GetBuildFailures()
    if success:
        write_json(
            result_path,
            {
                "success": True,
                "fingerprint": fingerprint,
                "request": request,
                "producer_pid": os.getpid(),
                "program_path": program_path,
            },
        )
        print(
            f"Published reusable successful build result for environment '{environment}' "
            f"and snapshot {fingerprint[:12]}."
        )
    try:
        if read_json(owner_path).get("pid") == os.getpid():
            os.unlink(owner_path)
    except FileNotFoundError:
        pass
    fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
    lock_file.close()


atexit.register(finish_build_coordination)
