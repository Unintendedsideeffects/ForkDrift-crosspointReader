#!/usr/bin/env python3
"""Keep the browser contract tied to firmware schema, limits and release assets."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / "scripts"))
import generate_language_template as template
import gen_i18n as generator

commit = "9482c32b88436ce3c0284aa8595000081ccaaebd"
artifacts = template.generate("test-template", commit)
catalog = json.loads(artifacts["language-template.json"])
source = root / "lib/I18n/translations/english.yaml"
english = generator.parse_yaml_file(str(source))
keys = [key for key in english if key.startswith("STR_")]
assert [entry["key"] for entry in catalog["entries"]] == keys
assert catalog["sourceCommit"] == commit and catalog["status"] == "local-fixture"
assert catalog["schemaVersion"] == 1 and catalog["format"] == "CrossInk flat YAML v1"
assert catalog["englishSourceSha256"] == hashlib.sha256(source.read_bytes()).hexdigest()
assert catalog["keysetSha256"] == hashlib.sha256(("\n".join(keys) + "\n").encode()).hexdigest()
assert catalog["template"]["sha256"] == hashlib.sha256(artifacts["english_template.yaml"]).hexdigest()
assert catalog["languages"] == [{"code": code, "name": name} for code, name in generator.known_languages()]
active = generator.find_used_string_keys([str(root / "src"), str(root / "lib")])
for entry in catalog["entries"]:
    assert entry["english"] == english[entry["key"]]
    assert entry["active"] == (entry["key"] in active)
    formatted = entry["key"].endswith("_FORMAT") or entry["key"] in generator.FORMAT_KEYS
    assert entry["formatted"] == formatted
    expected = generator.format_signature(entry["english"]) if formatted else None
    assert entry["signature"] == (list(expected) if expected is not None else None)

# The exported budget is the production on-flash layout, not just text length.
header = (root / "lib/LanguageCache/LanguageCache.h").read_text()
parser = (root / "lib/LanguageCache/LanguageCache.cpp").read_text()
assert f'SLOT_SIZE = {catalog["limits"]["cacheBytes"]}' in header
assert f'HEADER_SIZE = {catalog["limits"]["cacheHeaderBytes"]}' in header
assert f'MAX_KEYS = {catalog["limits"]["maxKeys"]}' in header
assert 'MAX_SOURCE_SIZE = 256 * 1024' in header and catalog["limits"]["sourceBytes"] == 256 * 1024
assert f'RECORD_SIZE = {catalog["limits"]["cacheRecordBytes"]}' in parser
assert f'LINE_SIZE = {catalog["limits"]["lineBytes"] + 1}' in parser
assert f'UNKNOWN_LIMIT = {catalog["limits"]["unknownKeys"]}' in parser
assert f'CODE_SIZE = {catalog["limits"]["codeBytes"] + 1}' in header
assert f'NAME_SIZE = {catalog["limits"]["nameBytes"] + 1}' in header
assert f'end - key > {catalog["limits"]["keyBytes"]}' in parser
assert f'count == {catalog["limits"]["printfArgs"]}' in parser
assert f'width > {catalog["limits"]["printfWidth"]}' in parser
assert f'precision > {catalog["limits"]["printfPrecision"]}' in parser

with tempfile.TemporaryDirectory(prefix="crossink-template-") as temporary:
    output = Path(temporary)
    template.write_artifacts(output, artifacts)
    yaml = generator.parse_yaml_file(str(output / "english_template.yaml"))
    assert all(yaml[key] == english[key] for key in keys)
    assert yaml["_template_keyset_sha256"] == catalog["keysetSha256"]
    assert yaml["_template_source_sha256"] == catalog["englishSourceSha256"]
    subprocess.run([sys.executable, str(root / "scripts/package_languages.py"), "--version", "test-template",
                    "--source-commit", commit, "--output", str(output / "languages.zip"),
                    "--template-output-dir", str(output / "pair")], check=True, capture_output=True)
    with zipfile.ZipFile(output / "languages.zip") as archive:
        for name, data in artifacts.items():
            assert archive.read(name) == data == (output / "pair" / name).read_bytes()
        assert archive.read("english-template.yaml") == artifacts["english_template.yaml"]
        manifest = json.loads(archive.read("manifest.json"))
        for name, checksum in manifest["sha256"].items():
            assert hashlib.sha256(archive.read(name)).hexdigest() == checksum
    release = json.loads(template.generate("release-test", commit, "release-build")["language-template.json"])
    assert release["status"] == "release-build"
for invalid in ["", "short", "f" * 39, "F" * 40]:
    try:
        template.generate("test", invalid)
        raise AssertionError("accepted invalid commit")
    except ValueError:
        pass
for invalid in ["", "bad\nversion", "bad\x00version"]:
    try:
        template.generate(invalid, commit)
        raise AssertionError("accepted invalid version")
    except ValueError:
        pass

for workflow in ["release.yml", "release_candidate.yml"]:
    text = (root / ".github/workflows" / workflow).read_text()
    assert '--source-commit "${{ github.sha }}" --status release-build' in text
    assert '/language-template/english_template.yaml' in text
    assert '/language-template/language-template.json' in text
assert "docs/languages/template" in (root / "site/scripts/sync_site_assets.mjs").read_text()
print("Language editor contract passed: exact source/keyset/template hashes, parser limits, schema and release artifacts")
