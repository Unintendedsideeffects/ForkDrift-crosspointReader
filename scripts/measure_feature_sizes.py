#!/usr/bin/env python3
"""
Measure actual firmware sizes for each feature to validate size estimates.

This script:
1. Builds the lean profile configuration (no optional features)
2. For each feature, builds (lean + transitive-deps + feature) and (lean + transitive-deps)
   and reports the incremental size cost of the feature itself
3. Tests for non-linear interactions in the full build
4. Outputs updated size estimates for generate_build_config.py

Features with hard firmware dependencies (e.g. opds requires calibre_sync which requires
integrations) are measured on top of those required features so the reported size reflects
only the code added by the feature itself, not its prerequisites.

A build cache keyed on frozenset(enabled_features) avoids redundant firmware compiles —
e.g. calibre_sync's full build and opds's base build are the same configuration.

Run this script whenever:
- A feature implementation changes significantly
- Adding a new feature
- Size estimates seem inaccurate
- Monthly validation check

Usage:
    python scripts/measure_feature_sizes.py
    python scripts/measure_feature_sizes.py --quick  # Skip full combination test
"""

import os
import subprocess
import sys
from pathlib import Path
from typing import Dict, FrozenSet
import argparse
import json
import shutil

sys.path.insert(0, str(Path(__file__).parent))
from generate_build_config import FEATURE_METADATA, FeatureMetadata  # noqa: E402

# Feature list ordered topologically (dependencies before dependents) to maximise
# build-cache hits: a dependency's full build == its dependent's base build.
FEATURES = [
    # --- No dependencies ---
    'bookerly_fonts',
    'notosans_fonts',
    'image_sleep',
    'markdown',
    'integrations',
    'background_server',
    'epub_support',
    'home_media_picker',
    'wifi_clock',
    'pokemon_party',
    'xtc_support',
    'ota_updates',
    'todo_planner',
    'anki_support',
    'remote_keyboard_input',
    'dark_mode',
    'user_fonts',
    'usb_mass_storage',
    'background_server_on_charge',
    'background_server_always',
    # --- Depend on features above ---
    'opendyslexic_fonts',       # requires: bookerly_fonts, notosans_fonts
    'book_images',              # firmware #error unless epub_support or markdown (see override)
    'koreader_sync',            # requires: integrations
    'calibre_sync',             # requires: integrations
    'opds',                     # requires: calibre_sync (→ integrations)
    'pokemon_wallpaper_plugin', # requires: image_sleep
    'hyphenation',              # requires: epub_support
    'lyra_theme',               # requires: home_media_picker
    'visual_cover_picker',      # requires: home_media_picker
    'web_wifi_setup',           # requires: background_server
    'ble_wifi_provisioning',    # requires: web_wifi_setup (→ background_server)
    'roman_clock_sleep',        # requires: wifi_clock
]

# For features with OR-style firmware constraints we can't express in FEATURE_METADATA,
# specify the canonical measurement base here. These are added to the transitive requires.
MEASUREMENT_BASE_OVERRIDES: Dict[str, list] = {
    # ENABLE_BOOK_IMAGES requires ENABLE_EPUB_SUPPORT=1 or ENABLE_MARKDOWN=1.
    # Use epub_support as the canonical representative base.
    'book_images': ['epub_support'],
}

if shutil.which("uv") is None:
    raise SystemExit("uv is required so feature-size builds use the pinned toolchain")


def maybe_uv_command(cmd: list[str]) -> list[str]:
    """Route Python/PlatformIO commands through uv."""
    if not cmd:
        return cmd
    if cmd[0] in ("python", "pio"):
        return ["uv", "run", *cmd]
    return cmd


# Use a dedicated build dir for measurement so local platformio.local.ini
# overrides (e.g. build_dir = /tmp/...) don't interfere with path resolution.
MEASURE_BUILD_DIR = ".pio-measure"


def run_command(cmd: list, capture=False, extra_env: dict | None = None) -> subprocess.CompletedProcess:
    """Run a shell command and return the result."""
    cmd = maybe_uv_command(cmd)
    env = {**os.environ, **(extra_env or {})}
    try:
        result = subprocess.run(
            cmd,
            capture_output=capture,
            text=True,
            check=True,
            env=env,
        )
        return result
    except subprocess.CalledProcessError as e:
        print(f"❌ Command failed: {' '.join(cmd)}")
        print(f"   Error: {e.stderr if capture else e}")
        raise


def build_configuration(features: Dict[str, bool], quiet=True) -> int:
    """Build a configuration and return actual firmware size in bytes."""
    print(f"   Building...", end='', flush=True)

    args = ['python', 'scripts/generate_build_config.py']
    for feature, enabled in features.items():
        if enabled:
            args.extend(['--enable', feature])

    run_command(args, capture=quiet)

    run_command(
        ['pio', 'run', '-e', 'custom'],
        capture=quiet,
        extra_env={'PLATFORMIO_BUILD_DIR': MEASURE_BUILD_DIR},
    )

    firmware_path = Path(MEASURE_BUILD_DIR) / 'custom' / 'firmware.bin'
    if not firmware_path.exists():
        raise FileNotFoundError(f"Firmware not found at {firmware_path}")

    size = firmware_path.stat().st_size
    print(f" {size / 1024 / 1024:.2f}MB")
    return size


def get_transitive_requires(feature: str) -> FrozenSet[str]:
    """Return the full set of features that must be enabled before measuring feature."""
    required: set[str] = set()
    meta = FEATURE_METADATA.get(feature, FeatureMetadata(implemented=True, stable=True))
    queue = list(meta.requires) + list(MEASUREMENT_BASE_OVERRIDES.get(feature, []))
    while queue:
        dep = queue.pop()
        if dep in required:
            continue
        required.add(dep)
        meta = FEATURE_METADATA.get(dep)
        if meta:
            queue.extend(meta.requires)
    return frozenset(required)


def measure_minimal_size() -> int:
    """Measure the size of a lean profile build (no optional features)."""
    print("\n📦 Step 1: Building lean profile configuration...")
    print("   (All optional features disabled)")
    return build_configuration({})


def measure_feature_sizes(lean_size: int) -> Dict[str, int]:
    """
    Measure the incremental size contribution of each feature.

    Each feature is measured on top of its full transitive dependency set so the
    reported KB reflects only the code that feature itself adds.
    """
    print("\n📊 Step 2: Measuring individual feature sizes...")
    print("   (Each delta = size of feature alone, measured on top of its required deps)")

    # Build cache: frozenset(enabled_features) -> firmware size in bytes
    # Pre-seed with the lean build (empty feature set).
    build_cache: Dict[FrozenSet[str], int] = {frozenset(): lean_size}

    feature_sizes: Dict[str, int] = {}

    for i, feature in enumerate(FEATURES, start=1):
        deps = get_transitive_requires(feature)
        base_key = deps
        full_key = deps | frozenset([feature])

        dep_label = f" [base: {'+'.join(sorted(deps))}]" if deps else ""
        print(f"   [{i}/{len(FEATURES)}] {feature}{dep_label}:", end=' ')

        # Ensure the base configuration is built (may already be cached).
        if base_key not in build_cache:
            print(f"\n      → building base {{{', '.join(sorted(deps))}}}...", end=' ')
            try:
                build_cache[base_key] = build_configuration({d: True for d in deps})
            except (subprocess.CalledProcessError, FileNotFoundError):
                print(" ⚠️  Base build failed, skipping feature")
                feature_sizes[feature] = 0
                continue

        base_size = build_cache[base_key]

        # Build base + feature (may already be cached if a prior feature shares this config).
        was_cached = full_key in build_cache
        if not was_cached:
            try:
                full_config = {d: True for d in deps}
                full_config[feature] = True
                build_cache[full_key] = build_configuration(full_config)
            except (subprocess.CalledProcessError, FileNotFoundError):
                print(" ⚠️  Build failed, using 0")
                feature_sizes[feature] = 0
                continue

        full_size = build_cache[full_key]
        delta_kb = int((full_size - base_size) / 1024)
        feature_sizes[feature] = delta_kb
        cached_label = " (cached)" if was_cached else ""
        print(f"Δ {delta_kb:+d} KB{cached_label}")

    return feature_sizes


def measure_full_size(lean_size: int, feature_sizes: Dict[str, int]) -> tuple[int, float]:
    """
    Measure full build size and detect non-linear effects.

    Returns:
        (actual_size, difference_kb) - Actual size and difference from linear estimate
    """
    print("\n🔬 Step 3: Testing full configuration for interaction effects...")

    print("   Building full configuration...")
    try:
        full_size = build_configuration({f: True for f in FEATURES})
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("   ⚠️  Full configuration build failed (likely too large for flash).")
        return 0, 0.0

    expected_size = lean_size + sum(feature_sizes.values()) * 1024
    difference = (full_size - expected_size) / 1024

    print(f"\n   Expected (linear sum of deltas): {expected_size / 1024 / 1024:.2f}MB")
    print(f"   Actual:                          {full_size / 1024 / 1024:.2f}MB")
    print(f"   Difference:                      {difference:+.0f}KB")

    if abs(difference) > 50:
        print("   ⚠️  WARNING: Significant non-linear size effects detected!")
        print("      Features may share code paths that weren't counted in dep-aware deltas.")
    else:
        print("   ✅ Size estimates are reasonably linear.")

    return full_size, difference


def print_summary(lean_size: int, feature_sizes: Dict[str, int], full_size: int):
    """Print summary of measurements."""
    print("\n" + "="*70)
    print("📋 MEASUREMENT SUMMARY")
    print("="*70)

    print(f"\n  BASE_SIZE_MB = {lean_size / 1024 / 1024:.2f}")
    print(f"\n  Feature deltas (incremental cost on top of each feature's deps):")
    print(f"  Update size_kb values in generate_build_config.py with these:")
    for feature in FEATURES:
        size_kb = feature_sizes.get(feature, 0)
        deps = get_transitive_requires(feature)
        dep_note = f"  # base: {'+'.join(sorted(deps))}" if deps else ""
        padding = 25 - len(feature)
        print(f"    '{feature}':{' '*padding}size_kb={size_kb},{dep_note}")

    print(f"\n  Full build size: {full_size / 1024 / 1024:.2f}MB")
    print(f"  Flash capacity:  6.4MB")
    if full_size > 0:
        flash_pct = (full_size / 1024 / 1024 / 6.4) * 100
        print(f"  Usage:           {flash_pct:.1f}%")

    if full_size > 6.4 * 1024 * 1024:
        print("\n  ⚠️  WARNING: Full build EXCEEDS flash capacity!")
        print("     Some feature combinations may not fit on device.")


def save_results(lean_size: int, feature_sizes: Dict[str, int], full_size: int, difference: float):
    """Save measurements to JSON file for tracking over time."""
    results = {
        'lean_size_mb': round(lean_size / 1024 / 1024, 2),
        'feature_deltas_kb': feature_sizes,
        'full_size_mb': round(full_size / 1024 / 1024, 2) if full_size else None,
        'non_linear_effect_kb': round(difference, 0),
        'flash_capacity_mb': 6.4,
        'full_build_fits': full_size <= 6.4 * 1024 * 1024 if full_size else None,
        'note': (
            'feature_deltas_kb values are incremental costs measured on top of each '
            'feature\'s transitive dependency set, not standalone sizes.'
        ),
    }

    output_file = Path('build_size_measurements.json')
    with open(output_file, 'w') as f:
        json.dump(results, f, indent=2)

    print(f"\n💾 Results saved to {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description='Measure actual firmware sizes for feature validation',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        '--quick',
        action='store_true',
        help='Skip full build test (faster, but less complete)'
    )

    args = parser.parse_args()

    # Count builds needed, accounting for shared bases (rough estimate).
    unique_bases = len({get_transitive_requires(f) for f in FEATURES})
    estimated_builds = 1 + unique_bases + len(FEATURES)

    print("🔍 CrossPoint Reader - Feature Size Measurement Tool")
    print("="*70)
    print()
    print("Each feature is measured on top of its required dependencies so the")
    print("reported size is the incremental cost of that feature alone.")
    print()
    print(f"Estimated builds (with caching): ~{estimated_builds}")
    print(f"Estimated time: ~{estimated_builds * 2} minutes")
    print()

    try:
        minimal_size = measure_minimal_size()
        feature_sizes = measure_feature_sizes(minimal_size)

        if args.quick:
            print("\n⏩ Skipping full build test (--quick mode)")
            full_size = minimal_size + sum(feature_sizes.values()) * 1024
            difference = 0.0
        else:
            full_size, difference = measure_full_size(minimal_size, feature_sizes)

        print_summary(minimal_size, feature_sizes, full_size)
        save_results(minimal_size, feature_sizes, full_size, difference)

        print("\n✅ Measurement complete!")
        print("\n💡 Next steps:")
        print("   1. Update size_kb values in scripts/generate_build_config.py")
        print("   2. Update BASE_SIZE_MB if lean profile size changed")
        print("   3. Commit build_size_measurements.json for tracking")

        return 0

    except subprocess.CalledProcessError:
        print("\n❌ Build failed. Cannot measure sizes.")
        print("   Fix compilation errors and try again.")
        return 1
    except FileNotFoundError as e:
        print(f"\n❌ Error: {e}")
        return 1
    except KeyboardInterrupt:
        print("\n\n⚠️  Measurement interrupted by user")
        return 130


if __name__ == '__main__':
    sys.exit(main())
