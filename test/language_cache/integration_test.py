#!/usr/bin/env python3
"""Exercise production I18n, its flash HAL, and generated data across fresh processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary, root = sys.argv[1], Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix="crossink-language-") as temporary:
    base = Path(temporary)
    languages = base / "sd/.crosspoint/languages"
    languages.mkdir(parents=True)
    env = dict(os.environ, CROSSINK_TEST_SD=str(base / "sd"), CROSSINK_LANGUAGE_FLASH=str(base / "flash.bin"))

    def run(*args):
        result = subprocess.run([binary, *map(str, args)], env=env, text=True, capture_output=True)
        assert result.returncode == 0, (args, result.stdout, result.stderr)
        return result.stdout.strip()

    assert run("boot", "ZZ-CUSTOM").startswith("EN|English|Settings|")
    assert not (base / "flash.bin").exists()  # boot never provisions or scans SD
    sample = languages / "custom.yaml"
    sample.write_text('_language_code: "ZZ-CUSTOM"\n_language_name: "My language"\n_direction: "rtl"\n'
                      '_keyboard: "HE"\nSTR_SETTINGS_TITLE: "Custom settings"\n', encoding="utf-8")
    assert "ZZ-CUSTOM|My language|0|" in run("scan")
    assert run("install", "EN", 0, "/.crosspoint/languages/custom.yaml") == "ZZ-CUSTOM|1"
    assert run("boot", "ZZ-CUSTOM", 1) == "ZZ-CUSTOM|My language|Custom settings|1"
    sample.write_text(sample.read_text().replace("Custom settings", "Revised settings"))
    assert "|Custom settings|" in run("boot", "ZZ-CUSTOM", 1)  # edits require apply
    assert run("install", "ZZ-CUSTOM", 1, "/.crosspoint/languages/custom.yaml") == "ZZ-CUSTOM|2"
    assert "|Custom settings|" in run("boot", "ZZ-CUSTOM", 1)  # failed selection save keeps old generation
    assert "|Revised settings|" in run("boot", "ZZ-CUSTOM", 2)
    sample.unlink()
    assert "|Revised settings|" in run("boot", "ZZ-CUSTOM", 2)
    assert "ZZ-CUSTOM|My language|0|" in run("scan", "ZZ-CUSTOM", 2)  # cached option without SD source
    assert run("boot", "ZZ-CUSTOM", 999).startswith("EN|English|Settings|")
    assert run("boot", "EN").startswith("EN|English|Settings|")

    # Install every existing starter file using the actual firmware schema.
    code, generation = "ZZ-CUSTOM", 2
    for path in sorted((root / "lib/I18n/translations").glob("*.yaml")):
        if path.name == "english.yaml":
            continue
        shutil.copy2(path, languages / path.name)
        installed = run("install", code, generation, f"/.crosspoint/languages/{path.name}")
        code, generation = installed.split("|")
        assert run("boot", code, generation).startswith(code + "|")
    # Duplicate identities remain visible and disabled, not arbitrarily selected.
    shutil.copy2(languages / "french.yaml", languages / "french-copy.yaml")
    assert any(line.startswith("FR|") and "|1|" in line for line in run("scan").splitlines())
    for allocation in ("catalog", "inspector"):
        result = subprocess.run([binary, "scan"], env=dict(env, CROSSINK_TEST_LANGUAGE_OOM=allocation),
                                text=True, capture_output=True)
        assert result.returncode == 5 and "out of memory" in result.stderr, result
    # All starter files fit the fixed arena together. Long user-controlled
    # names/filenames and excessive file counts fail without aborting or clipping.
    for path in languages.iterdir():
        path.unlink()
    for i in range(16):
        (languages / (f"long-{i}-" + "x" * 90 + ".yaml")).write_text(
            f'_language_code: "LONG-{i}"\n_language_name: "' + "x" * 90 + '"\nSTR_SETTINGS_TITLE: "Test"\n')
    result = subprocess.run([binary, "scan"], env=env, text=True, capture_output=True)
    assert result.returncode == 5 and "exceeds limits" in result.stderr, result
    for path in languages.iterdir():
        path.unlink()
    for i in range(65):
        (languages / f"{i}.yaml").write_text(f'_language_code: "X{i}"\n_language_name: "X{i}"\n')
    result = subprocess.run([binary, "scan"], env=env, text=True, capture_output=True)
    assert result.returncode == 5 and "exceeds limits" in result.stderr, result
print("I18n integration passed: stable pointers, restart/apply, missing SD files, fallback, and all starter languages")
