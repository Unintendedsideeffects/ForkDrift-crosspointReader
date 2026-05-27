import os
import shutil
import stat
import subprocess
import textwrap
from pathlib import Path


def make_executable(path: Path) -> None:
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_uv_stub(bin_dir: Path) -> None:
    stub_path = bin_dir / "uv"
    stub_path.write_text(
        textwrap.dedent(
            """\
            #!/usr/bin/env python3
            import os
            import sys
            from pathlib import Path

            log_path = Path(os.environ["UV_TEST_LOG"])
            cwd = Path.cwd()
            args = sys.argv[1:]
            with log_path.open("a", encoding="utf-8") as handle:
                handle.write(" ".join(args) + "\\n")

            if args[:4] == ["run", "python", "scripts/generate_build_config.py", "--profile"] and len(args) >= 5:
                profile = args[4]
                (cwd / "platformio-custom.ini").write_text(
                    f"# Selected profile: {profile}\\n",
                    encoding="utf-8",
                )
                raise SystemExit(0)

            if args[:2] == ["run", "pio"]:
                build_env = "custom" if "-e" in args and "custom" in args else "default"
                (cwd / ".uv-last-env").write_text(build_env, encoding="utf-8")
                build_dir = Path("/tmp/crosspoint-pio-build/copilot-wrapper-test") / build_env
                build_dir.mkdir(parents=True, exist_ok=True)
                (build_dir / "firmware.bin").write_bytes(b"firmware")
                raise SystemExit(0)

            if len(args) >= 3 and args[:3] == ["run", "python", "scripts/name_firmware_artifact.py"]:
                build_env_file = cwd / ".uv-last-env"
                build_env = build_env_file.read_text(encoding="utf-8").strip() if build_env_file.exists() else "default"
                profile = ""
                if build_env == "custom":
                    for line in (cwd / "platformio-custom.ini").read_text(encoding="utf-8").splitlines():
                        if line.startswith("# Selected profile:"):
                            profile = line.split(":", 1)[1].strip()
                            break
                output_dir = cwd / "firmware"
                output_dir.mkdir(exist_ok=True)
                artifact_name = "firmware-20260527-0000-0000000.bin"
                if profile:
                    artifact_name = f"firmware-{profile}-20260527-0000-0000000.bin"
                artifact_path = output_dir / artifact_name
                artifact_path.write_bytes(b"firmware")
                latest = cwd / "firmware-latest.bin"
                if latest.exists() or latest.is_symlink():
                    latest.unlink()
                latest.symlink_to(os.path.relpath(artifact_path, cwd))
                raise SystemExit(0)

            print(f"unexpected uv invocation: {' '.join(args)}", file=sys.stderr)
            raise SystemExit(1)
            """
        ),
        encoding="utf-8",
    )
    make_executable(stub_path)


def prepare_wrapper_fixture(tmp_path: Path) -> tuple[Path, Path]:
    repo_root = Path(__file__).resolve().parents[2]
    fixture_root = tmp_path / "repo"
    fixture_root.mkdir()

    for script_name in ("build-firmware.sh", "build_firmware.sh"):
        target = fixture_root / script_name
        shutil.copy2(repo_root / script_name, target)
        make_executable(target)

    firmware_dir = fixture_root / "crosspoint-reader"
    (firmware_dir / "scripts").mkdir(parents=True)
    (firmware_dir / "firmware").mkdir()

    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    write_uv_stub(bin_dir)

    return fixture_root, bin_dir


def run_wrapper(tmp_path: Path, script_name: str, *args: str) -> tuple[subprocess.CompletedProcess[str], list[str], Path]:
    fixture_root, bin_dir = prepare_wrapper_fixture(tmp_path)
    log_path = tmp_path / "uv.log"
    env = os.environ.copy()
    env["PATH"] = f"{bin_dir}:{env['PATH']}"
    env["UV_TEST_LOG"] = str(log_path)

    result = subprocess.run(
        [str(fixture_root / script_name), *args],
        cwd=fixture_root,
        capture_output=True,
        text=True,
        env=env,
    )
    lines = log_path.read_text(encoding="utf-8").splitlines()
    return result, lines, fixture_root


def test_profile_argument_generates_matching_custom_build(tmp_path):
    result, lines, fixture_root = run_wrapper(tmp_path, "build_firmware.sh", "full")

    assert result.returncode == 0, result.stderr + result.stdout
    assert "run python scripts/generate_build_config.py --profile full" in lines
    assert "run pio run -e custom" in lines
    assert "Environment: custom (profile: full)" in result.stdout
    generated = (fixture_root / "crosspoint-reader" / "platformio-custom.ini").read_text(encoding="utf-8")
    assert "# Selected profile: full" in generated


def test_default_invocation_preserves_default_environment(tmp_path):
    result, lines, _ = run_wrapper(tmp_path, "build-firmware.sh")

    assert result.returncode == 0, result.stderr + result.stdout
    assert "run pio run" in lines
    assert all("generate_build_config.py" not in line for line in lines)
    assert "Environment: default" in result.stdout


def test_all_argument_builds_all_predefined_profiles(tmp_path):
    result, lines, fixture_root = run_wrapper(tmp_path, "build-firmware.sh", "all")

    assert result.returncode == 0, result.stderr + result.stdout
    assert result.stdout.count("Environment: custom (profile:") == 3
    assert "Profiles: lean standard full" in result.stdout
    assert lines.count("run python scripts/generate_build_config.py --profile lean") == 1
    assert lines.count("run python scripts/generate_build_config.py --profile standard") == 1
    assert lines.count("run python scripts/generate_build_config.py --profile full") == 1
    assert lines.count("run pio run -e custom") == 3
    generated = (fixture_root / "crosspoint-reader" / "platformio-custom.ini").read_text(encoding="utf-8")
    assert "# Selected profile: full" in generated
