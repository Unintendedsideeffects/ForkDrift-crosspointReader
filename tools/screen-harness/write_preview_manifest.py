#!/usr/bin/env python3
"""Write the screen preview manifest used by the configurator."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

PREVIEW_SPECS = [
    {"stem": "01_boot", "label": "Boot"},
    {"stem": "02_home_classic", "label": "Home - Classic"},
    {"stem": "03_home_lyra", "label": "Home - Lyra"},
    {"stem": "04_home_visual_covers", "label": "Home - Visual Covers"},
    {"stem": "05_home_forkdrift", "label": "Home - ForkDrift"},
    {"stem": "06_home_pokemon_party", "label": "Home - Pokemon Party"},
    {"stem": "07_home_minimal", "label": "Home - Minimal"},
    {"stem": "08_home_lyra_carousel", "label": "Home - Lyra Carousel"},
    {"stem": "09_settings", "label": "Settings"},
    {"stem": "10_factory_reset", "label": "Factory Reset"},
    {"stem": "11_reader_mock", "label": "Reader", "type": "live-reader"},
    {"stem": "12_feature_store_mock", "label": "Feature Store"},
    {"stem": "13_sleep_dark", "label": "Sleep - Dark"},
    {"stem": "14_sleep_light", "label": "Sleep - Light"},
    {"stem": "15_sleep_custom", "label": "Sleep - Custom Image"},
    {"stem": "16_sleep_transparent", "label": "Sleep - Transparent"},
]

HOME_THEME_PREVIEWS = {
    "0": "02_home_classic",
    "1": "03_home_lyra",
    "2": "04_home_visual_covers",
    "3": "05_home_forkdrift",
    "4": "06_home_pokemon_party",
    "5": "07_home_minimal",
    "6": "08_home_lyra_carousel",
}


def prune_stale_pngs(out_dir: Path) -> None:
    expected = {spec["stem"] for spec in PREVIEW_SPECS}
    for png in out_dir.glob("*.png"):
        if png.stem not in expected:
            png.unlink()


def build_manifest(out_dir: Path) -> dict[str, object]:
    labels_by_stem = {spec["stem"]: spec["label"] for spec in PREVIEW_SPECS}
    files = []
    for spec in PREVIEW_SPECS:
        entry_type = spec.get("type")
        if entry_type:
            # Live entries always appear; the browser renders them dynamically
            files.append({"name": f"{spec['stem']}.png", "label": spec["label"], "type": entry_type})
        else:
            png = out_dir / f"{spec['stem']}.png"
            if png.exists():
                files.append({"name": png.name, "label": spec["label"]})

    home_themes = {}
    for theme_value, stem in HOME_THEME_PREVIEWS.items():
        png = out_dir / f"{stem}.png"
        if png.exists():
            home_themes[theme_value] = {"name": png.name, "label": labels_by_stem[stem]}

    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "files": files,
        "home_themes": home_themes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--prune-only", action="store_true")
    args = parser.parse_args()

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    prune_stale_pngs(out_dir)
    if args.prune_only:
        return 0

    manifest = build_manifest(out_dir)
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
