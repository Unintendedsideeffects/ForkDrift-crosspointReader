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
    # Legacy filesystem data must not block installation through the real HAL.
    # The preceding data region and unselected slot stay untouched.
    old_flash = bytearray(b"\xff" * 0x360000)
    old_flash[8] = 0x42
    old_flash[-0x20000:] = b"\xa5" * 0x20000
    (base / "flash.bin").write_bytes(old_flash)
    assert run("boot", "ZZ-CUSTOM").startswith("EN|English|Settings|")
    assert (base / "flash.bin").read_bytes() == old_flash
    assert "ZZ-CUSTOM|My language|0|" in run("scan")
    assert run("install", "EN", 0, "/.crosspoint/languages/custom.yaml") == "ZZ-CUSTOM|1"
    installed_flash = (base / "flash.bin").read_bytes()
    assert installed_flash[:-0x20000] == old_flash[:-0x20000]
    assert installed_flash[-0x10000:] == old_flash[-0x10000:]
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

    # Every inherited printf contract must reject incompatible SD translations.
    contracts = {
        "STR_HOLD_FOR_KEYBOARD": ("%u", "Hold %s for Keyboard"),
        "STR_LIBRARY_SCAN_COUNT": ("%s", "Reading your books... %u"),
        "STR_LIBRARY_FILES_COUNT": ("%s", "%u files"),
        "STR_STATS_UPLOAD_COUNTS": ("%s", "Global: %u\nBooks: %u\nSkipped: %u"),
        "STR_FOLDER_SYNC_COUNTS": ("%s", "Synced: %u · Skipped: %u · Failed: %u"),
    }
    sample.write_text('_language_code: "TEST-FMT"\n_language_name: "Unsafe format fixture"\n' +
                      ''.join(f'{key}: "{bad}"\n' for key, (bad, _) in contracts.items()))
    fmt_code, fmt_generation = run("install", "ZZ-CUSTOM", 2, "/.crosspoint/languages/custom.yaml").split("|")
    for key, (_, expected) in contracts.items():
        assert run("lookup", fmt_code, fmt_generation, key) == expected
    sample.unlink()

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
    result = subprocess.run([binary, "scan"], env=dict(env, CROSSINK_TEST_DIRECTORY_OOM="1"),
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
