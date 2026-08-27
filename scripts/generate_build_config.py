#!/usr/bin/env python3
"""
Generate custom PlatformIO build configuration for CrossPoint Reader.
Allows selective enabling/disabling of features to reduce firmware size.

Profiles: lean, standard, full (see --list-features).
"""

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import feature_manifest


def git_commit_count() -> str:
    """Return HEAD's commit count (capped at 5 digits), or 0 if unavailable."""
    try:
        result = subprocess.run(
            ["git", "rev-list", "--count", "HEAD"],
            cwd=Path(__file__).resolve().parent.parent,
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode != 0:
            return "0"
        count = int(result.stdout.strip())
        return str(min(count, 99999))
    except Exception:
        return "0"


class Feature:
    """Represents a toggleable firmware feature."""

    def __init__(self, name: str, flag: str, size_kb: int, description: str):
        self.name = name
        self.flag = flag
        self.size_kb = size_kb
        self.description = description

_manifest_path = Path(__file__).parent.parent / "config" / "features.yaml"
_manifest = feature_manifest.load_manifest(_manifest_path)

FEATURES = {}
for _key, _feat in _manifest.features.items():
    FEATURES[_key] = Feature(
        name=_feat.label,
        flag=_feat.macro,
        size_kb=_feat.estimated_size_kib,
        description=_feat.description,
    )


# Feature metadata and dependencies
class FeatureMetadata:
    """Metadata about feature implementation and dependencies."""

    def __init__(self, implemented: bool, stable: bool, requires: List[str] = None,
                 requires_any: List[str] = None,
                 conflicts: List[str] = None, recommends: List[str] = None):
        self.implemented = implemented
        self.stable = stable
        self.requires = requires or []
        self.requires_any = requires_any or []
        self.conflicts = conflicts or []
        self.recommends = recommends or []


FEATURE_METADATA = {}
for _key, _feat in _manifest.features.items():
    FEATURE_METADATA[_key] = FeatureMetadata(
        implemented=_feat.implemented,
        stable=_feat.stable,
        requires=list(_feat.requires_all),
        requires_any=list(_feat.requires_any),
        conflicts=list(_feat.conflicts),
        recommends=list(_feat.recommends),
    )


def validate_feature_configuration(enabled_features: Dict[str, bool]) -> tuple[List[str], List[str]]:
    """
    Validate feature dependencies and conflicts.

    Returns:
        (errors, warnings) - Lists of error and warning messages
    """
    errors = []
    warnings = []

    for feature_key, enabled in enabled_features.items():
        if not enabled:
            continue

        metadata = FEATURE_METADATA.get(feature_key)
        if not metadata:
            warnings.append(f"Unknown feature: {feature_key}")
            continue

        # Check if feature is implemented
        if not metadata.implemented:
            errors.append(
                f"{FEATURES[feature_key].name} is not yet implemented. "
                f"This feature flag exists but the code is not complete. "
                f"Build will fail or feature will not work."
            )

        # Check required dependencies (all must be enabled)
        for required in metadata.requires:
            if not enabled_features.get(required, False):
                errors.append(
                    f"{FEATURES[feature_key].name} requires {FEATURES[required].name} to be enabled"
                )

        # Check requires_any dependencies (at least one must be enabled)
        if metadata.requires_any:
            if not any(enabled_features.get(r, False) for r in metadata.requires_any):
                names = ', '.join(FEATURES[r].name for r in metadata.requires_any if r in FEATURES)
                errors.append(
                    f"{FEATURES[feature_key].name} requires at least one of: {names}"
                )

        # Check conflicts
        for conflict in metadata.conflicts:
            if enabled_features.get(conflict, False):
                errors.append(
                    f"{FEATURES[feature_key].name} conflicts with {FEATURES[conflict].name}. "
                    f"These features cannot be enabled together."
                )

        # Check recommendations
        for recommended in metadata.recommends:
            if not enabled_features.get(recommended, False):
                warnings.append(
                    f"{FEATURES[feature_key].name} works best with {FEATURES[recommended].name} enabled"
                )

    return errors, warnings


def apply_dependency_defaults(enabled_features: Dict[str, bool]) -> List[str]:
    """
    Auto-enable required parent features for enabled downstream features.

    Returns:
        List of warning messages describing auto-enabled dependencies.
    """
    warnings: List[str] = []
    changed = True

    while changed:
        changed = False
        for feature_key, enabled in enabled_features.items():
            if not enabled:
                continue

            metadata = FEATURE_METADATA.get(feature_key)
            if not metadata:
                continue

            for required in metadata.requires:
                if not enabled_features.get(required, False):
                    enabled_features[required] = True
                    warnings.append(
                        f"Auto-enabled {FEATURES[required].name} because {FEATURES[feature_key].name} is enabled"
                    )
                    changed = True

    return warnings


PROFILES = {}
for _pname, _prof in _manifest.profiles.items():
    PROFILES[_pname] = {
        'description': _prof.description,
        'features': dict(_prof.features),
    }


def empty_feature_state() -> Dict[str, bool]:
    """Return a fully-initialized feature map with all options disabled."""
    return {feature_key: False for feature_key in FEATURES.keys()}


def calculate_size(enabled_features: Dict[str, bool]) -> float:
    """Calculate estimated firmware size in MB."""
    base_size_mb = 2.68  # Lean profile size baseline (measured)

    for feature_key, enabled in enabled_features.items():
        if enabled:
            base_size_mb += FEATURES[feature_key].size_kb / 1024.0

    return base_size_mb


def generate_build_flags(enabled_features: Dict[str, bool]) -> List[str]:
    """Generate PlatformIO build flags for the given feature set."""
    flags = []

    for feature_key, feature in FEATURES.items():
        value = 1 if enabled_features.get(feature_key, False) else 0
        flags.append(f'-D{feature.flag}={value}')

    return flags


def generate_platformio_ini(enabled_features: Dict[str, bool], output_path: Path, profile_name: str):
    """Generate platformio-custom.ini with custom build configuration."""

    build_flags = generate_build_flags(enabled_features)
    estimated_size = calculate_size(enabled_features)

    # Profile token embedded in the version string must be a plain lowercase
    # [a-z0-9_]+ token for the on-device parser (e.g. "full+overrides" -> "full_overrides").
    version_profile = "".join(c if (c.isalnum()) else "_" for c in profile_name).lower()
    if not version_profile:
        version_profile = "custom"
    version_profile = version_profile[:32]

    # Generate feature list for comment
    enabled_list = []
    disabled_list = []
    for feature_key in FEATURES.keys():
        feature = FEATURES[feature_key]
        enabled = enabled_features.get(feature_key, False)
        if enabled:
            enabled_list.append(f'{feature.name} ({feature.size_kb}K)')
        else:
            disabled_list.append(feature.name)

    enabled_comment = '\n'.join(f'#   - {name}' for name in enabled_list) if enabled_list else '#   (none)'
    disabled_comment = '\n'.join(f'#   - {name}' for name in disabled_list) if disabled_list else '#   (none)'

    content = f"""# Custom CrossPoint Reader Build Configuration
# Generated by generate_build_config.py
#
# Selected profile: {profile_name}
# Estimated firmware size: ~{estimated_size:.1f}MB
#
# Enabled features:
{enabled_comment}
#
# Disabled features:
{disabled_comment}

[env:custom]
extends = base
build_flags =
  ${{base.build_flags}}
  -DCROSSPOINT_VERSION=\\"${{crosspoint.version}}-dev+{git_commit_count()}-{version_profile}\\"
  -DENABLE_SERIAL_LOG
  -DLOG_LEVEL=0
{chr(10).join(f'  {flag}' for flag in build_flags)}
"""

    output_path.write_text(content)
    print(f"Generated {output_path}")
    for name in enabled_list:
        print(f"  ✓ {name}")
    if disabled_list:
        print(f"\nDisabled features:")
        for name in disabled_list:
            print(f"  ✗ {name}")


def main():
    parser = argparse.ArgumentParser(
        description='Generate custom CrossPoint Reader build configuration',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Use a profile
  %(prog)s --profile lean
  %(prog)s --profile standard
  %(prog)s --profile full

  # Enable specific features
  %(prog)s --enable bookerly_fonts --enable image_sleep

  # Disable specific features from full profile
  %(prog)s --profile full --disable markdown

  # List available features
  %(prog)s --list-features
"""
    )

    parser.add_argument(
        '--profile',
        choices=sorted(PROFILES.keys()),
        help='Use a predefined feature profile'
    )

    parser.add_argument(
        '--enable',
        action='append',
        choices=list(FEATURES.keys()),
        help='Enable a specific feature (can be used multiple times)'
    )

    parser.add_argument(
        '--disable',
        action='append',
        choices=list(FEATURES.keys()),
        help='Disable a specific feature (can be used multiple times)'
    )

    parser.add_argument(
        '--output',
        type=Path,
        default=Path('platformio-custom.ini'),
        help='Output file path (default: platformio-custom.ini)'
    )

    parser.add_argument(
        '--list-features',
        action='store_true',
        help='List all available features and exit'
    )

    args = parser.parse_args()

    if args.list_features:
        print("Available features:\n")
        for key, feature in FEATURES.items():
            print(f"  {key:20} - {feature.name}")
            print(f"  {'':20}   {feature.description}")
            print(f"  {'':20}   Size impact: ~{feature.size_kb}K")
            print()

        print("\nAvailable profiles:\n")
        for profile_name, profile_info in PROFILES.items():
            print(f"  {profile_name:10} - {profile_info['description']}")

        return 0

    requested_profile = args.profile

    # Determine enabled features
    enabled_features = empty_feature_state()
    selected_profile = "custom"
    if requested_profile:
        enabled_features.update(PROFILES[requested_profile]['features'])
        selected_profile = requested_profile
        print(f"Using profile: {requested_profile}")
        print(f"  {PROFILES[requested_profile]['description']}")

    # Apply --enable flags
    if args.enable:
        for feature_key in args.enable:
            enabled_features[feature_key] = True
        if selected_profile != "custom":
            selected_profile = f"{selected_profile}+overrides"

    # Apply --disable flags
    if args.disable:
        for feature_key in args.disable:
            enabled_features[feature_key] = False
        if selected_profile != "custom":
            selected_profile = f"{selected_profile}+overrides"

    dependency_warnings = apply_dependency_defaults(enabled_features)

    # Validate configuration
    errors, warnings = validate_feature_configuration(enabled_features)
    warnings = dependency_warnings + warnings

    # Print warnings
    if warnings:
        print("\n⚠️  Warnings:")
        for warning in warnings:
            print(f"  • {warning}")
        print()

    # Print errors and exit if any
    if errors:
        print("\n❌ Configuration errors:")
        for error in errors:
            print(f"  • {error}")
        print("\nConfiguration is invalid. Please fix the errors above.")
        return 1

    # Generate configuration
    generate_platformio_ini(enabled_features, args.output, selected_profile)

    print("\nTo build: uv run pio run -e custom")

    return 0


if __name__ == '__main__':
    sys.exit(main())
