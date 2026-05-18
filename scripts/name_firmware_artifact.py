# ─────────────────────────────────────────────────────────────────────────────
# Firmware artifact filename contract — PRODUCER half.
#
# This script *produces* the on-disk firmware-*.bin filename. The *recognizer*
# half is src/util/FirmwareArtifactName.h (firmware_artifact::isMatchingName),
# which the device runs at boot to find an SD-root local-update binary.
#
# These two MUST stay in lockstep: any filename build_artifact_name() can emit
# must be accepted by isMatchingName(), or the on-device boot → SD-root → flash
# path silently ignores the dropped binary. The grammar is documented in that
# header and pinned by test/host/test_firmware_artifact_name.cpp — when you
# change the naming scheme here, update all three together.
# ─────────────────────────────────────────────────────────────────────────────

import argparse
import datetime
import os
import shutil
import subprocess
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]  # noqa: F821
except Exception:
    env = None

MAX_NAMED_FIRMWARE_ARTIFACTS = 3
LATEST_FIRMWARE_LINK = "firmware-latest.bin"
FIRMWARE_OUTPUT_DIR = "firmware"


def get_build_date() -> str:
    build_date = os.environ.get("BUILD_DATE")
    if build_date:
        return "".join(ch for ch in build_date if ch.isdigit())[:8]
    return datetime.datetime.utcnow().strftime("%Y%m%d")


def is_ci() -> bool:
    # CI sets these; CI trees are always clean and one build == one commit, so the
    # original date+sha name is unambiguous there. Keep it unchanged for CI.
    return bool(os.environ.get("GITHUB_SHA") or os.environ.get("BUILD_DATE") or os.environ.get("CI"))


def is_dirty(project_dir: Path) -> bool:
    # "Dirty" = tracked source differs from HEAD. Exclude the I18n files: they are
    # regenerated on every build, so including them would flag essentially every
    # local build as dirty and make the marker meaningless. (HTML *.generated.h are
    # gitignored, so they never show in a tracked diff.) Untracked-only new files
    # are intentionally not counted.
    try:
        rc = subprocess.call(
            [
                "git",
                "diff",
                "--quiet",
                "HEAD",
                "--",
                ".",
                ":(exclude)lib/I18n/I18nKeys.h",
                ":(exclude)lib/I18n/I18nStrings.h",
                ":(exclude)lib/I18n/I18nStrings.cpp",
            ],
            cwd=project_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=5,
        )
        return rc != 0  # git diff --quiet exits non-zero when differences exist
    except Exception:
        return False


def build_artifact_name(project_dir: Path) -> str:
    date = get_build_date()
    sha = get_short_sha(project_dir)
    if is_ci():
        return f"firmware-{date}-{sha}.bin"
    # Local builds: add HHMM so same-day/same-sha rebuilds don't silently
    # overwrite each other, and a -dirty marker when the tree has uncommitted
    # source changes (so a flashed binary is traceable to exact state).
    time_part = datetime.datetime.now().strftime("%H%M")
    dirty = "-dirty" if is_dirty(project_dir) else ""
    return f"firmware-{date}-{time_part}-{sha}{dirty}.bin"


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


def prune_old_named_firmware(output_dir: Path) -> None:
    artifacts = sorted(
        output_dir.glob("firmware-*.bin"),
        key=lambda path: (path.stat().st_mtime, path.name),
        reverse=True,
    )
    for artifact in artifacts[MAX_NAMED_FIRMWARE_ARTIFACTS:]:
        artifact.unlink()
        print(f">> removed old firmware artifact: {artifact}")


def update_latest_firmware_link(project_dir: Path, artifact_path: Path) -> None:
    link_path = project_dir / LATEST_FIRMWARE_LINK
    link_target = os.path.relpath(artifact_path, project_dir)

    # os.path.lexists() is True for a broken symlink too (Path.exists() is not),
    # which is exactly the state that previously got stuck and left no pointer.
    if os.path.lexists(link_path):
        if link_path.is_symlink() or link_path.is_file():
            link_path.unlink()
        else:
            print(f">> skipped latest firmware symlink; path exists and is not a file/symlink: {link_path}")
            return

    link_path.symlink_to(link_target)
    print(f">> latest firmware symlink: {link_path} -> {link_target}")


def copy_named_firmware_from_build_dir(project_dir: Path, build_dir: Path) -> bool:
    firmware_path = build_dir / "firmware.bin"
    if not firmware_path.exists():
        return False

    output_dir = project_dir / FIRMWARE_OUTPUT_DIR
    output_dir.mkdir(exist_ok=True)

    artifact_name = build_artifact_name(project_dir)
    artifact_path = output_dir / artifact_name

    shutil.copy2(firmware_path, artifact_path)
    os.utime(artifact_path, None)
    print(f">> firmware artifact: {artifact_path}")
    update_latest_firmware_link(project_dir, artifact_path)
    prune_old_named_firmware(output_dir)
    return True


def copy_named_firmware(target, source, env):
    copy_named_firmware_from_build_dir(
        project_dir=Path(env["PROJECT_DIR"]),
        build_dir=Path(env.subst("$BUILD_DIR")),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Copy the latest PlatformIO firmware.bin to a named artifact")
    parser.add_argument("--project-dir", required=True, help="Path to the project root")
    parser.add_argument("--build-dir", required=True, help="Path to the PlatformIO build directory")
    args = parser.parse_args()

    copied = copy_named_firmware_from_build_dir(
        project_dir=Path(args.project_dir).resolve(),
        build_dir=Path(args.build_dir).resolve(),
    )
    if not copied:
        print(f">> firmware.bin not found in {Path(args.build_dir).resolve()}")
        return 1
    return 0


if env is not None:
    firmware_path = Path(env.subst("$BUILD_DIR")) / "firmware.bin"
    env.AddPostAction(str(firmware_path), copy_named_firmware)


if __name__ == "__main__":
    raise SystemExit(main())
