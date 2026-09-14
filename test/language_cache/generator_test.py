#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

root = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location("generator", root / "scripts/gen_i18n.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
with tempfile.TemporaryDirectory(prefix="crossink-language-generator-") as temporary:
    base = Path(temporary)
    (base / "english.yaml").write_bytes((root / "lib/I18n/translations/english.yaml").read_bytes())
    (base / "broken-community.yaml").write_text("this is not valid YAML")
    languages, names, keys, _, _ = generator.load_translations(str(base))
    assert languages == ["EN"] and names == ["English"] and "STR_SETTINGS_TITLE" in keys
    compatibility = generator.known_languages()
    assert compatibility[0] == ["EN", "English"]
    assert dict(compatibility)["PT"] == "Português (Brasil)"
    assert dict(compatibility)["PT2"] == "Português (Portugal)"
    assert dict(compatibility)["SI"] == "Slovenščina"
    for name in ["first.zip", "second.zip"]:
        subprocess.run([sys.executable, str(root / "scripts/package_languages.py"), "--version", "test",
                        "--output", str(base / name)], check=True, capture_output=True)
    assert (base / "first.zip").read_bytes() == (base / "second.zip").read_bytes()
    with zipfile.ZipFile(base / "first.zip") as archive:
        names = archive.namelist()
        assert "english-template.yaml" in names
        assert ".crosspoint/languages/english.yaml" not in names
        assert len([n for n in names if n.startswith(".crosspoint/languages/")]) == 27
        assert json.loads(archive.read("manifest.json"))["firmware"] == "test"
print("Generator and packaging passed: English-only dependency, legacy identities, and reproducible release archive")
