#!/usr/bin/env python3
"""
Pre-build validation hook for PlatformIO.

This script runs before building the custom environment to validate
that the configuration is valid. It prevents wasting build time on
configurations that will fail.

Validates:
- Feature flags are correctly set
- No unimplemented features are enabled
- No conflicting features are enabled
- Estimated size doesn't wildly exceed capacity

Called automatically by PlatformIO when building custom environment.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

import feature_manifest

MANIFEST_PATH = Path(__file__).parent.parent / "config" / "features.yaml"
manifest = feature_manifest.load_manifest(MANIFEST_PATH)
BUILD_FEATURES = dict(manifest.features)


# Available only when invoked by PlatformIO as an extra script.
try:
    Import("env")  # type: ignore[name-defined]
    PLATFORMIO_ENV = env
except Exception:
    PLATFORMIO_ENV = None

# Terminal colors
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
GREEN = '\033[0;32m'
NC = '\033[0m'  # No Color

REQUIRED_FLAGS = [feature.macro for feature in BUILD_FEATURES.values()]


def _extract_flags(config: str) -> dict[str, bool]:
    """Extract -DENABLE_* flags from platformio-custom.ini content."""
    flags: dict[str, bool] = {}
    for match in re.finditer(r'-D(ENABLE_\w+)=([01])', config):
        flags[match.group(1)] = match.group(2) == '1'
    return flags


def _extract_estimated_size(config: str) -> float:
    """Extract estimated firmware size in MB from generated config comments."""
    size_match = re.search(r'Estimated firmware size: ~([\d.]+)MB', config)
    return float(size_match.group(1)) if size_match else 0.0


def _extract_profile(config: str) -> str:
    """Extract selected profile from generated config comments."""
    profile_match = re.search(r'Selected profile:\s*(.+)', config)
    if not profile_match:
        return "unknown"
    return profile_match.group(1).strip()


def validate_config(config_path: Path) -> int:
    """Validate the custom build configuration."""
    if not config_path.exists():
        print(f"{RED}ERROR: platformio-custom.ini not found{NC}")
        print("Run: uv run python scripts/generate_build_config.py --profile standard")
        return 1

    # Read config
    config = config_path.read_text()

    # Extract feature flags
    flags = _extract_flags(config)
    selected_profile = _extract_profile(config)

    # Extract estimated size
    estimated_size_mb = _extract_estimated_size(config)

    # Validation checks
    errors: list[str] = []
    warnings: list[str] = []

    missing_flags = [flag for flag in REQUIRED_FLAGS if flag not in flags]
    if missing_flags:
        errors.append(
            "Missing required feature flags in generated config: "
            + ", ".join(missing_flags)
            + ". Re-generate platformio-custom.ini with scripts/generate_build_config.py."
        )

    if estimated_size_mb <= 0:
        warnings.append(
            "Estimated firmware size was not found in config comments. "
            "Size validation may be inaccurate."
        )

    # Check for unimplemented features
    if flags.get('ENABLE_KOREADER_SYNC', False):
        # Check if this is still marked as unimplemented
        # This would be updated as features are implemented
        pass  # Currently implemented

    if flags.get('ENABLE_CALIBRE_SYNC', False):
        # Check if this is still marked as unimplemented
        pass  # Currently implemented

    if (flags.get('ENABLE_KOREADER_SYNC', False) or flags.get('ENABLE_CALIBRE_SYNC', False)) and not flags.get(
        'ENABLE_INTEGRATIONS', False
    ):
        errors.append(
            "KOReader/Calibre integrations require ENABLE_INTEGRATIONS=1. "
            "Re-run generate_build_config.py or enable integrations explicitly."
        )

    # Check flash capacity
    MAX_FLASH_MB = 6.4
    if estimated_size_mb > MAX_FLASH_MB:
        warnings.append(
            f"Estimated size {estimated_size_mb:.1f}MB exceeds flash capacity {MAX_FLASH_MB}MB. "
            f"Build may fail during linking."
        )

    if estimated_size_mb > MAX_FLASH_MB + 0.2:
        errors.append(
            f"Estimated size {estimated_size_mb:.1f}MB significantly exceeds flash capacity. "
            f"This configuration will not fit on device."
        )

    # Print results
    enabled_count = sum(1 for v in flags.values() if v)
    print(f"{GREEN}[PRE-BUILD] Validating custom configuration...{NC}")
    print(f"  Profile:          {selected_profile}")
    print(f"  Plugins enabled: {enabled_count}/{len(flags)}")
    print(f"  Estimated size:   {estimated_size_mb:.1f}MB / {MAX_FLASH_MB}MB")

    if warnings:
        print(f"\n{YELLOW}⚠️  Warnings:{NC}")
        for warning in warnings:
            print(f"  • {warning}")

    if errors:
        print(f"\n{RED}❌ Validation errors:{NC}")
        for error in errors:
            print(f"  • {error}")
        print(f"\n{RED}Build aborted due to validation errors.{NC}")
        return 1

    print(f"{GREEN}✅ Configuration validated{NC}\n")
    return 0


def resolve_target_env(explicit_env: str | None) -> str:
    """Resolve the active PlatformIO environment."""
    if explicit_env:
        return explicit_env
    if PLATFORMIO_ENV is not None:
        return str(PLATFORMIO_ENV.get("PIOENV", ""))
    return os.environ.get("PIOENV", "custom")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate generated custom build flags")
    parser.add_argument("--env", help="Override environment name (default: auto-detect)")
    parser.add_argument("--config", default="platformio-custom.ini", help="Path to generated custom config")
    args = parser.parse_args(argv)

    target_env = resolve_target_env(args.env)
    if target_env != "custom":
        print(f"[PRE-BUILD] Skipping custom configuration validation for env '{target_env}'")
        return 0

    try:
        return validate_config(Path(args.config))
    except Exception as err:
        print(f"{RED}ERROR during validation: {err}{NC}")
        return 1


if __name__ == "__main__":
    sys.exit(main())

if PLATFORMIO_ENV is not None:
    result = main([])
    if result != 0:
        raise SystemExit(result)
