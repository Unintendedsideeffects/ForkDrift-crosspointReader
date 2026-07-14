import shutil
import subprocess
from pathlib import Path

import pytest


SCRIPT = Path(__file__).parents[1] / "scripts" / "check_feature_boundaries.sh"


def _git(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args], cwd=repo, check=True, text=True, capture_output=True
    )


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    (tmp_path / "scripts").mkdir()
    (tmp_path / "src").mkdir()
    shutil.copy2(SCRIPT, tmp_path / "scripts" / SCRIPT.name)
    (tmp_path / "src" / "App.cpp").write_text("#if ENABLE_EXISTING\n#endif\n")
    _git(tmp_path, "init", "-q")
    _git(tmp_path, "config", "user.email", "ci@example.invalid")
    _git(tmp_path, "config", "user.name", "CI Test")
    _git(tmp_path, "add", "scripts", "src")
    _git(tmp_path, "commit", "-qm", "baseline")
    return tmp_path


def _check(repo: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["bash", "scripts/check_feature_boundaries.sh", "HEAD"],
        cwd=repo,
        text=True,
        capture_output=True,
    )


def test_existing_debt_and_removal_pass(repo: Path):
    assert _check(repo).returncode == 0
    (repo / "src" / "App.cpp").write_text("int app = 0;\n")
    assert _check(repo).returncode == 0


def test_new_guard_fails(repo: Path):
    with (repo / "src" / "App.cpp").open("a") as source:
        source.write("#if ENABLE_NEW\n#endif\n")
    result = _check(repo)
    assert result.returncode == 1
    assert "src/App.cpp:#if ENABLE_NEW" in result.stdout


def test_duplicate_guard_fails(repo: Path):
    with (repo / "src" / "App.cpp").open("a") as source:
        source.write("#if ENABLE_EXISTING\n#endif\n")
    assert _check(repo).returncode == 1


def test_untracked_backup_is_ignored(repo: Path):
    (repo / "src" / "App.cpp.orig").write_text("#if ENABLE_BACKUP\n#endif\n")
    assert _check(repo).returncode == 0


@pytest.mark.parametrize(
    "path",
    [
        "src/core/Core.cpp",
        "src/features/example/Registration.cpp",
        "src/network/ota/OtaWebCheck.cpp",
    ],
)
def test_approved_paths_are_ignored(repo: Path, path: str):
    target = repo / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("#if ENABLE_ALLOWED\n#endif\n")
    _git(repo, "add", path)
    assert _check(repo).returncode == 0
