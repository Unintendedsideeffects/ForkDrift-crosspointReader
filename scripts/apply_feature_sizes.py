#!/usr/bin/env python3
"""
Apply measured feature sizes from build_size_measurements.json to source files.

Updates hardcoded size values in:
  - config/features.yaml              (estimated_size_kib: NNNN)
  - docs/configurator/index.html      (const BASE_SIZE_MB = X.XX)

Usage:
    python scripts/apply_feature_sizes.py
    python scripts/apply_feature_sizes.py --input path/to/measurements.json
    python scripts/apply_feature_sizes.py --dry-run
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INPUT = REPO_ROOT / "build_size_measurements.json"
MANIFEST = REPO_ROOT / "config" / "features.yaml"
CONFIGURATOR_HTML = REPO_ROOT / "docs" / "configurator" / "index.html"
ARTIFACT_GENERATOR = REPO_ROOT / "scripts" / "generate_feature_artifacts.py"


def patch_yaml_feature_size(content: str, feature: str, new_size_kb: int) -> tuple[str, bool]:
    """Replace estimated_size_kib: NNNN under a specific feature key."""
    pattern = re.compile(
        rf"^(  {re.escape(feature)}:\n(?:    [^\n]*\n)*?"
        rf"    estimated_size_kib:\s*)(-?\d+)$",
        re.MULTILINE,
    )
    new_content, count = pattern.subn(rf"\g<1>{new_size_kb}", content)
    return new_content, count > 0


def patch_html_base_size(content: str, new_base_mb: float) -> tuple[str, bool]:
    """Replace const BASE_SIZE_MB = X.XX in the HTML."""
    pattern = re.compile(r"(const\s+BASE_SIZE_MB\s*=\s*)(\d+\.\d+)")
    new_content, count = pattern.subn(rf"\g<1>{new_base_mb:.2f}", content)
    return new_content, count > 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply measured feature sizes to source files"
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_INPUT,
        help=f"Path to build_size_measurements.json (default: {DEFAULT_INPUT})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without writing files",
    )
    args = parser.parse_args()

    if not args.input.exists():
        print(f"Error: measurements file not found: {args.input}")
        return 1

    with open(args.input) as f:
        data = json.load(f)

    try:
        feature_sizes: dict[str, int] = data["feature_deltas_kb"]
        base_size_mb: float = float(data["lean_size_mb"])
    except KeyError as exc:
        print(
            "Error: measurements file is missing required keys. "
            "Expected feature_deltas_kb and lean_size_mb."
        )
        print(f"Missing key: {exc.args[0]}")
        return 1

    print(f"Loaded measurements from {args.input}")
    print(f"  base size:  {base_size_mb:.2f} MB")
    print(f"  features:   {len(feature_sizes)}")
    print()

    # ── Patch config/features.yaml ──────────────────────────────
    yaml_content = MANIFEST.read_text()
    yaml_original = yaml_content
    yaml_changes: list[str] = []

    for feature, size_kb in feature_sizes.items():
        yaml_content, changed = patch_yaml_feature_size(yaml_content, feature, size_kb)
        if changed:
            yaml_changes.append(f"  {feature}: estimated_size_kib={size_kb}")

    if yaml_changes:
        print("config/features.yaml:")
        for c in yaml_changes:
            print(c)
    else:
        print("config/features.yaml: no changes needed")

    # ── Patch docs/configurator/index.html ──────────────────────────
    html_content = CONFIGURATOR_HTML.read_text()
    html_original = html_content
    html_changes: list[str] = []

    html_content, changed = patch_html_base_size(html_content, base_size_mb)
    if changed:
        html_changes.append(f"  BASE_SIZE_MB={base_size_mb:.2f}")

    if html_changes:
        print("docs/configurator/index.html:")
        for c in html_changes:
            print(c)
    else:
        print("docs/configurator/index.html: no changes needed")

    # ── Write files ─────────────────────────────────────────────────
    any_changes = (yaml_content != yaml_original) or (html_content != html_original)

    if not any_changes:
        print("\nAll sizes already up to date.")
        return 0

    if args.dry_run:
        print("\n(dry run — no files modified)")
        return 0

    if yaml_content != yaml_original:
        MANIFEST.write_text(yaml_content)
        print(f"\nWrote {MANIFEST}")

    if html_content != html_original:
        CONFIGURATOR_HTML.write_text(html_content)
        print(f"Wrote {CONFIGURATOR_HTML}")

    if yaml_content != yaml_original:
        generated = subprocess.run([sys.executable, str(ARTIFACT_GENERATOR)])
        if generated.returncode != 0:
            print("Error: feature sizes were updated, but artifact regeneration failed.")
            return generated.returncode

    return 0


if __name__ == "__main__":
    sys.exit(main())
