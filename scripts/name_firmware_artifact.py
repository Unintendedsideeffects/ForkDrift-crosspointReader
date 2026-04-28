Import("env")  # noqa: F821

import datetime
import os
import shutil
import subprocess
from pathlib import Path


def get_build_date() -> str:
    build_date = os.environ.get("BUILD_DATE")
    if build_date:
        return "".join(ch for ch in build_date if ch.isdigit())[:8]
    return datetime.datetime.utcnow().strftime("%Y%m%d")


def get_short_sha(project_dir: Path) -> str:
    github_sha = os.environ.get("GITHUB_SHA")
    if github_sha:
        return github_sha[:7]

    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short=7", "HEAD"],
            cwd=project_dir,
            text=True,
            stderr=subprocess.PIPE,
            timeout=5,
        ).strip()
    except Exception:
        return "0000000"


def copy_named_firmware(target, source, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware_path = build_dir / "firmware.bin"
    if not firmware_path.exists():
        return

    project_dir = Path(env["PROJECT_DIR"])
    artifact_name = f"firmware-{get_build_date()}-{get_short_sha(project_dir)}.bin"
    artifact_path = build_dir / artifact_name

    if firmware_path.resolve() == artifact_path.resolve():
        return

    shutil.copy2(firmware_path, artifact_path)
    print(f">> firmware artifact: {artifact_path}")


firmware_path = Path(env.subst("$BUILD_DIR")) / "firmware.bin"
env.AddPostAction(str(firmware_path), copy_named_firmware)
