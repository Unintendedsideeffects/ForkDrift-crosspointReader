import subprocess
import sys
from pathlib import Path


def run_generate_build_config(*args, cwd, output_path=None):
    command = [sys.executable, "scripts/generate_build_config.py", *args]
    if output_path is not None:
        command.extend(["--output", str(output_path)])
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True)


def test_standard_profile_generates_representative_flags(tmp_path):
    root = Path(__file__).resolve().parents[1]
    output = tmp_path / "platformio-custom.ini"

    result = run_generate_build_config("--profile", "standard", cwd=root, output_path=output)

    assert result.returncode == 0, result.stderr + result.stdout
    generated = output.read_text()
    assert "# Selected profile: standard" in generated
    assert "-DENABLE_EPUB_SUPPORT=1" in generated
    assert "-DENABLE_BACKGROUND_SERVER=1" in generated
    assert "-DENABLE_KOREADER_SYNC=0" in generated
    assert "Using profile: standard" in result.stdout


def test_profile_overrides_preserve_selected_profile_name(tmp_path):
    root = Path(__file__).resolve().parents[1]
    output = tmp_path / "platformio-custom.ini"

    result = run_generate_build_config(
        "--profile",
        "full",
        "--disable",
        "markdown",
        cwd=root,
        output_path=output,
    )

    assert result.returncode == 0, result.stderr + result.stdout
    generated = output.read_text()
    assert "# Selected profile: full+overrides" in generated
    assert "-DENABLE_MARKDOWN=0" in generated


def test_anki_enables_text_selection_dependency(tmp_path):
    root = Path(__file__).resolve().parents[1]
    output = tmp_path / "platformio-custom.ini"

    result = run_generate_build_config("--enable", "anki_support", cwd=root, output_path=output)

    assert result.returncode == 0, result.stderr + result.stdout
    generated = output.read_text()
    assert "-DENABLE_ANKI_SUPPORT=1" in generated
    assert "-DENABLE_TEXT_SELECTION=1" in generated
    assert "-DENABLE_EPUB_SUPPORT=1" in generated


def test_full_profile_keeps_anki_capture_dependencies_enabled(tmp_path):
    root = Path(__file__).resolve().parents[1]
    output = tmp_path / "platformio-custom.ini"

    result = run_generate_build_config("--profile", "full", cwd=root, output_path=output)

    assert result.returncode == 0, result.stderr + result.stdout
    generated = output.read_text()
    assert "-DENABLE_ANKI_SUPPORT=1" in generated
    assert "-DENABLE_TEXT_SELECTION=1" in generated
    assert "-DENABLE_EPUB_SUPPORT=1" in generated


def test_legacy_profile_alias_is_rejected(tmp_path):
    root = Path(__file__).resolve().parents[1]
    output = tmp_path / "platformio-custom.ini"

    result = run_generate_build_config("--profile", "minimal", cwd=root, output_path=output)

    assert result.returncode != 0
    assert "invalid choice" in result.stderr
    assert not output.exists()


def test_removed_compatibility_flags_are_rejected(tmp_path):
    root = Path(__file__).resolve().parents[1]

    preset = run_generate_build_config("--preset", "standard", cwd=root, output_path=tmp_path / "preset.ini")
    list_plugins = run_generate_build_config("--list-plugins", cwd=root)

    assert preset.returncode != 0
    assert "unrecognized arguments: --preset" in preset.stderr
    assert list_plugins.returncode != 0
    assert "unrecognized arguments: --list-plugins" in list_plugins.stderr


def test_configurator_and_build_tooling_feature_metadata_stay_in_sync():
    root = Path(__file__).resolve().parents[1]

    result = subprocess.run(
        [sys.executable, "scripts/check_feature_key_sync.py"],
        cwd=root,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr + result.stdout


def test_configurator_does_not_accept_removed_extended_fonts_query_alias():
    root = Path(__file__).resolve().parents[1]
    configurator = root / "docs/configurator/index.html"

    html = configurator.read_text()

    assert 'params.get("extended_fonts")' not in html
    assert "legacyExtendedFonts" not in html


def test_ota_catalog_uses_explicit_font_feature_keys():
    root = Path(__file__).resolve().parents[1]
    catalog = root / "docs/ota/feature-store-catalog.json"

    text = catalog.read_text()

    assert "extended_fonts" not in text
    assert "bookerly_fonts" in text
    assert "lexenddeca_fonts" in text
