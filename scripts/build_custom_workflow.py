#!/usr/bin/env python3
"""
Workflow helper for build-custom.yml.

Keeps GitHub Actions input parsing compact while delegating feature truth to
generate_build_config.py.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

import feature_manifest

MANIFEST_PATH = Path(__file__).parent.parent / "config" / "features.yaml"
manifest = feature_manifest.load_manifest(MANIFEST_PATH)
FEATURES = {k: v for k, v in manifest.features.items()}
PROFILES = {k: v for k, v in manifest.profiles.items()}


def parse_feature_list(raw: str) -> list[str]:
    return [token for token in re.split(r"[\s,]+", raw.strip()) if token]


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate platformio-custom.ini for the custom workflow")
    parser.add_argument(
        "--profile",
        default="standard",
        help="Profile name: lean, standard, full, or custom (default: standard)",
    )
    parser.add_argument(
        "--enable-features",
        default="",
        help="Comma/space-separated feature keys to enable (see generate_build_config.py --list-features)",
    )
    parser.add_argument(
        "--disable-features",
        default="",
        help="Comma/space-separated feature keys to disable",
    )
    parser.add_argument(
        "--output",
        default="platformio-custom.ini",
        help="Output path for platformio-custom.ini (default: platformio-custom.ini)",
    )
    args = parser.parse_args()

    requested_profile = args.profile
    if requested_profile != "custom" and requested_profile not in PROFILES:
        print(f"Unknown profile: {args.profile}", file=sys.stderr)
        return 1

    enable_features = parse_feature_list(args.enable_features)
    disable_features = parse_feature_list(args.disable_features)

    unknown = sorted({feature for feature in enable_features + disable_features if feature not in FEATURES})
    if unknown:
        print(
            "Unknown feature key(s): " + ", ".join(unknown) + ". "
            "Use feature keys from config/features.yaml.",
            file=sys.stderr,
        )
        return 1

    command: list[str] = [sys.executable, "scripts/generate_build_config.py"]
    if requested_profile != "custom":
        command.extend(["--profile", requested_profile])
    for feature in enable_features:
        command.extend(["--enable", feature])
    for feature in disable_features:
        command.extend(["--disable", feature])
    command.extend(["--output", args.output])

    print("Executing:", " ".join(command))
    completed = subprocess.run(command)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
