#!/usr/bin/env python3
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    # Check that test_all_combinations.sh is in sync with features.yaml
    sys.path.insert(0, str(ROOT))
    from scripts.feature_manifest import load_manifest

    manifest_path = ROOT / "config" / "features.yaml"
    manifest = load_manifest(manifest_path)
    manifest_keys = set(manifest.features.keys())

    test_sh_path = ROOT / "scripts" / "test_all_combinations.sh"
    text = test_sh_path.read_text()
    match = re.search(r"^\s*FEATURES=\(([^)]*)\)", text, re.M)
    if not match:
        print("Feature synchronization check failed: test_all_combinations.sh missing FEATURES=()")
        return 1

    sh_keys = set()
    for dq, sq in re.findall(r'"([^"]+)"|\'([^\']+)\'', match.group(1)):
        sh_keys.add(dq or sq)

    missing = manifest_keys - sh_keys
    extra = sh_keys - manifest_keys

    if missing or extra:
        print("Feature synchronization check failed:")
        if missing:
            print(f"  test_all_combinations.sh missing keys: {', '.join(sorted(missing))}")
        if extra:
            print(f"  test_all_combinations.sh unexpected keys: {', '.join(sorted(extra))}")
        return 1

    # Check that generated JS/JSON are up to date
    res = subprocess.run(
        [sys.executable, "scripts/generate_feature_artifacts.py", "--check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        print("Feature synchronization check failed:")
        details = (res.stdout + res.stderr).strip()
        if details:
            print(details)
        return 1

    print(
        "Feature synchronization check passed: features aligned across tooling, "
        "configurator metadata, and the authoritative manifest."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
