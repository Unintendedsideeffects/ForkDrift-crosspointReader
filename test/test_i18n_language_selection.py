"""Per-build UI language selection (custom_i18n_languages).

The committed I18nKeys.h / I18nStrings.h must be identical for every selection
(they are a pure function of the YAML), and the saved-setting meaning of every
Language value must not move. Only the uncommitted I18nStrings.cpp may differ.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRANSLATIONS = ROOT / "lib" / "I18n" / "translations"


def run_gen_i18n(output_dir, *args):
    command = [sys.executable, "scripts/gen_i18n.py", str(TRANSLATIONS), str(output_dir), *args]
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=True)


def test_subset_keeps_committed_headers_identical(tmp_path):
    full = tmp_path / "full"
    subset = tmp_path / "subset"
    full.mkdir()
    subset.mkdir()

    assert run_gen_i18n(full).returncode == 0
    result = run_gen_i18n(subset, "--languages", "FRENCH,de")
    assert result.returncode == 0, result.stdout + result.stderr

    for header in ("I18nKeys.h", "I18nStrings.h"):
        assert (full / header).read_bytes() == (subset / header).read_bytes(), header


def test_omitted_languages_become_stubs_and_english_is_always_kept(tmp_path):
    result = run_gen_i18n(tmp_path, "--languages", "FRENCH")
    assert result.returncode == 0, result.stdout + result.stderr

    cpp = (tmp_path / "I18nStrings.cpp").read_text(encoding="utf-8")
    assert "    true,  // EN" in cpp
    assert "    true,  // FRENCH" in cpp
    assert "    false,  // DE" in cpp
    # Omitted languages keep their symbols (the committed header declares them) as 1-element stubs.
    assert 'const char STRINGS_DE_DATA[] = "";' in cpp
    assert "const uint16_t OFFSETS_DE[] = {0};" in cpp
    # And no size assert for a stub table, which would fail to compile.
    assert "OFFSETS_DE size mismatch" not in cpp
    assert "OFFSETS_FRENCH size mismatch" in cpp


def test_unknown_language_code_fails_instead_of_dropping_silently(tmp_path):
    result = run_gen_i18n(tmp_path, "--languages", "FRENCH,KLINGON")
    assert result.returncode != 0
    assert "KLINGON" in result.stdout + result.stderr


def test_build_config_emits_language_selection(tmp_path):
    output = tmp_path / "platformio-custom.ini"
    result = subprocess.run(
        [sys.executable, "scripts/generate_build_config.py", "--profile", "standard", "--languages", "french,DE",
         "--output", str(output)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "custom_i18n_languages = EN,FRENCH,DE" in output.read_text()


def test_build_config_without_languages_keeps_every_language(tmp_path):
    output = tmp_path / "platformio-custom.ini"
    result = subprocess.run(
        [sys.executable, "scripts/generate_build_config.py", "--profile", "standard", "--output", str(output)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "custom_i18n_languages" not in output.read_text()
