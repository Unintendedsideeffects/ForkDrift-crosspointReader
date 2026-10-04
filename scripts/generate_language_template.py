#!/usr/bin/env python3
"""Generate the atomic editor contract and canonical YAML from firmware English."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from gen_i18n import FORMAT_KEYS, find_used_string_keys, format_signature, known_languages, parse_yaml_file

ROOT = Path(__file__).resolve().parents[1]
LIMITS = {
    "sourceBytes": 262144, "lineBytes": 2047, "keyBytes": 127, "unknownKeys": 256,
    "codeBytes": 31, "nameBytes": 95, "cacheBytes": 65536, "cacheHeaderBytes": 256,
    "cacheRecordBytes": 18, "maxKeys": 2048, "printfArgs": 32, "printfWidth": 1024,
    "printfPrecision": 128,
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def generate(version, source_commit=None, status="local-fixture"):
    """Return both release artifacts from one source and one commit snapshot."""
    if source_commit is None:
        source_commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("source commit must be the full lowercase Git SHA")
    if status not in ("local-fixture", "release-build"):
        raise ValueError("invalid provenance status")
    # The device accepts only quoted scalars and rejects control bytes.
    if not version or any(ord(c) < 32 or ord(c) == 127 for c in version):
        raise ValueError("version must be nonempty printable text")
    source = ROOT / "lib/I18n/translations/english.yaml"
    source_bytes = source.read_bytes()
    english = parse_yaml_file(str(source))
    if english.get("_language_code") != "EN":
        raise ValueError("canonical source must be English")
    keys = [key for key in english if key.startswith("STR_")]
    if any(not key.startswith("_") and not key.startswith("STR_") for key in english):
        raise ValueError("unsupported English key")
    active = find_used_string_keys([str(ROOT / "src"), str(ROOT / "lib")])
    if active - set(keys):
        raise ValueError("firmware references keys absent from English")
    if len(active) > LIMITS["maxKeys"]:
        raise ValueError("firmware key count exceeds parser limit")
    keyset_hash = sha256(("\n".join(keys) + "\n").encode("utf-8"))
    # Preserve the current source key IDs and order. Added metadata stays before strings.
    prefix = (f'_firmware_version: {json.dumps(version, ensure_ascii=False)}\n'
              f'_template_source_sha256: "{sha256(source_bytes)}"\n'
              f'_template_keyset_sha256: "{keyset_hash}"\n')
    template = prefix.encode("utf-8") + source_bytes
    if len(template) > LIMITS["sourceBytes"] or any(
            len(line) > LIMITS["lineBytes"] for line in template.split(b"\n")):
        raise ValueError("English template exceeds device source limits")
    entries = []
    for key in keys:
        formatted = key.endswith("_FORMAT") or key in FORMAT_KEYS
        signature = format_signature(english[key]) if formatted else None
        if formatted and signature is None:
            raise ValueError(f"invalid English printf contract: {key}")
        entries.append({"key": key, "english": english[key], "formatted": formatted,
                        "signature": list(signature) if signature is not None else None,
                        "active": key in active})
    contract = {
        "schemaVersion": 1, "format": "CrossInk flat YAML v1", "firmwareVersion": version,
        "status": status, "sourceCommit": source_commit,
        "englishSourceSha256": sha256(source_bytes), "keysetSha256": keyset_hash,
        "template": {"filename": "english_template.yaml", "sha256": sha256(template)},
        "limits": LIMITS,
        "languages": [{"code": code, "name": name} for code, name in known_languages()],
        "entries": entries,
    }
    data = (json.dumps(contract, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    return {"english_template.yaml": template, "language-template.json": data}


def write_artifacts(output, artifacts):
    output.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        (output / name).write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--source-commit")
    parser.add_argument("--status", choices=["local-fixture", "release-build"], default="local-fixture")
    args = parser.parse_args()
    write_artifacts(args.output_dir, generate(args.version, args.source_commit, args.status))
    print(f"Generated language template: {args.output_dir}")
