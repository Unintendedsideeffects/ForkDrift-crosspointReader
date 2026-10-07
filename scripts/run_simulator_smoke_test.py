#!/usr/bin/env python3
"""Build and run the simulator smoke test against an isolated fs_ directory."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOOK = ROOT / "test" / "epubs" / "test_reader_rendering_matrix.epub"
CRASH_PATTERNS = (
    "std::bad_alloc",
    "terminating due to uncaught exception",
    "Assertion failed",
    "Segmentation fault",
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
)
THEMES = {
    "classic": 0,
    "lyra": 1,
    "lyra-extended": 2,
    "lyra_extended": 2,
    "lyra3": 2,
    "lyra-3-covers": 2,
    "roundedraff": 3,
    "rounded-raff": 3,
    "lyra-carousel": 4,
    "lyra_carousel": 4,
    "carousel": 4,
    "dashboard": 6,
    "cover-grid": 7,
}


def program_path(env_name: str) -> Path:
    build_dir = Path(os.environ.get("PLATFORMIO_BUILD_DIR", ROOT / ".pio" / "build"))
    return build_dir / env_name / "program"


def build_simulator(env_name: str) -> None:
    print(f"Building {env_name} simulator...", flush=True)
    proc = subprocess.run(["pio", "run", "-e", env_name, "-j1"], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def prepare_fs(temp_root: Path, book: Path) -> str:
    books_dir = temp_root / "fs_" / "books"
    books_dir.mkdir(parents=True, exist_ok=True)

    target = books_dir / book.name
    shutil.copy2(book, target)
    return f"/books/{book.name}"


def run_smoke(args: argparse.Namespace) -> int:
    book = Path(args.book).resolve()
    if not book.exists():
        print(f"Smoke test book not found: {book}", file=sys.stderr)
        return 2

    if args.build:
        build_simulator(args.env)

    program = program_path(args.env)
    if not program.exists():
        print(f"Simulator binary not found: {program}", file=sys.stderr)
        print(f"Run: pio run -e {args.env}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="crossink-sim-smoke-") as temp_dir_name:
        temp_root = Path(temp_dir_name)
        simulator_book_path = prepare_fs(temp_root, book)

        if args.font_dir:
            shutil.copytree(Path(args.font_dir), temp_root / "fs_" / "fonts", dirs_exist_ok=True)
        env = os.environ.copy()
        if args.screen_calibration:
            env["CROSSINK_SIMULATOR_SMOKE_CALIBRATION"] = "1"
            env["CROSSINK_CALIBRATION_PATH"] = str(temp_root / "device-calibration.nvs")
        if args.status_bar_lifecycle:
            env["CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_LIFECYCLE"] = "1"
        if args.status_bar_feature:
            env["CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_FEATURE"] = "1"
        if args.status_bar_captures:
            env["CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_CAPTURES"] = str(Path(args.status_bar_captures).resolve())
        if args.dictionary:
            dictionary = temp_root / "fs_" / "dictionary-smoke"
            dictionary.mkdir()
            words = sorted(["accord", "aime", "après", "d'accord", "est", "histoire", "inspecteur", "école"])
            index = bytearray()
            definitions = bytearray()
            for word in words:
                definition = f"Definition of {word}".encode()
                index.extend(word.encode() + b"\0" + struct.pack(">II", len(definitions), len(definition)))
                definitions.extend(definition)
            (dictionary / "dict.idx").write_bytes(index)
            (dictionary / "dict.dict").write_bytes(definitions)
            (dictionary / "dict.ifo").write_text(
                "StarDict's dict ifo file\nversion=2.4.2\nbookname=French smoke\n"
                f"wordcount={len(words)}\nidxfilesize={len(index)}\nsametypesequence=m\n"
            )
            env["CROSSINK_SIMULATOR_SMOKE_DICTIONARY"] = "1"
        if args.font_dir and args.font_family:
            env["CROSSINK_SIMULATOR_SMOKE_ISOLATED_FONTS"] = "1"
        if args.font_family:
            env["CROSSINK_SIMULATOR_SMOKE_FONT_FAMILY"] = args.font_family
        env["CROSSINK_SIMULATOR_SMOKE_TEST"] = "1"
        env["CROSSINK_SIMULATOR_SMOKE_BOOK"] = simulator_book_path
        env["CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS"] = str(args.page_turns)
        if args.context_menu_sync_return:
            env["CROSSINK_SIMULATOR_SMOKE_CONTEXT_MENU_SYNC_RETURN"] = args.context_menu_sync_return
            env["CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_RETURN"] = "1"
        if args.file_browser_sync_return:
            env["CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_RETURN"] = "1"
        if args.frontlight_sync:
            env["CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_SYNC"] = "1"
        if args.frontlight_layout:
            env["CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_LAYOUT"] = "1"
        if args.frontlight_captures:
            capture_dir = Path(args.frontlight_captures).resolve()
            capture_dir.mkdir(parents=True, exist_ok=True)
            env["CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_CAPTURES"] = str(capture_dir)
        if args.support_export:
            env["CROSSINK_SIMULATOR_SMOKE_SUPPORT_EXPORT"] = "1"
        if args.about:
            env["CROSSINK_SIMULATOR_SMOKE_ABOUT"] = "1"
        if args.about_captures:
            env["CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_CAPTURES"] = str(Path(args.about_captures).resolve())
        if args.sync_server_captures:
            env["CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_CAPTURES"] = str(Path(args.sync_server_captures).resolve())
            env["CROSSINK_SIMULATOR_SMOKE_SYNC_SERVER_CAPTURES"] = "1"
        if args.home_themes:
            env["CROSSINK_SIMULATOR_SMOKE_HOME_THEMES"] = "1"
        if args.theme:
            env["CROSSINK_SIMULATOR_SMOKE_THEME"] = str(THEMES[args.theme])
        if args.headless:
            env.setdefault("SDL_VIDEODRIVER", "dummy")

        print(f"Running simulator smoke test with isolated fs_: {temp_root / 'fs_'}", flush=True)
        proc = subprocess.run(
            [str(program)],
            cwd=temp_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout if args.timeout is not None else (180 if args.frontlight_layout or args.about or args.support_export else 120),
        )

    print(proc.stdout, end="")

    if proc.returncode != 0:
        print(f"Simulator smoke test failed with exit code {proc.returncode}", file=sys.stderr)
        return proc.returncode

    for pattern in CRASH_PATTERNS:
        if pattern in proc.stdout:
            print(f"Simulator smoke test output contained crash pattern: {pattern}", file=sys.stderr)
            return 2

    if "Simulator smoke test passed" not in proc.stdout:
        print("Simulator smoke test did not print its success marker", file=sys.stderr)
        return 2

    if args.font_family:
        tab_change = proc.stdout.find("Reader Menu tab changed after TTF Native selection")
        reader_return = proc.stdout.find("Reader restored after TTF Native selection", tab_change)
        if tab_change < 0 or reader_return < 0 or "Loading file:" not in proc.stdout[tab_change:reader_return]:
            print("Reader did not reload its page after changing TTF options and switching tabs", file=sys.stderr)
            return 2

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--book", default=str(DEFAULT_BOOK), help="EPUB fixture to copy into the isolated simulator fs_")
    parser.add_argument("--env", choices=("simulator", "simulator-X3", "x4-classic-simulator", "sticky-simulator", "x4-pro-simulator"), default="simulator",
                        help="PlatformIO simulator environment to build and run")
    parser.add_argument("--status-bar-lifecycle", action="store_true", help="Exercise reader settings return and reflow")
    parser.add_argument("--status-bar-feature", action="store_true", help="Check status bar visibility, independent sizes, disk reload and rendered theme/orientation matrix")
    parser.add_argument("--screen-calibration", action="store_true", help="Check screen calibration button/touch editing, save, cancel, reset and rotations")
    parser.add_argument("--status-bar-captures", help="Directory for status bar feature framebuffer captures (PGM)")
    parser.add_argument("--dictionary", action="store_true", help="Check French contractions and exact dictionary matches")
    parser.add_argument("--font-dir", help="Font fixtures copied into isolated /fonts")
    parser.add_argument("--font-family", help="Exercise custom-font size and dictionary lifecycle")
    parser.add_argument("--timeout", type=int, help="Seconds before the simulator run is treated as hung (default: 120, or 180 for frontlight layout, about, or support export)")
    parser.add_argument("--page-turns", type=int, default=2, help="Number of EPUB page-forward taps to run")
    parser.add_argument("--theme", choices=sorted(THEMES), help="UI theme to use during the smoke test")
    parser.add_argument("--context-menu-sync-return", choices=["file-browser", "library"], help="Check context-menu sync return across network reboots")
    parser.add_argument("--file-browser-sync-return", action="store_true", help="Check browser sync return across network reboots")
    parser.add_argument("--frontlight-sync", action="store_true", help="Check frontlight sync outside the reader with stats enabled and disabled (X4 Pro)")
    parser.add_argument("--frontlight-layout", action="store_true", help="Check frontlight drawer bounds and handle taps across scales, orientations and themes (X4 Pro)")
    parser.add_argument("--frontlight-captures", help="Directory for frontlight layout framebuffer captures (PGM)")
    parser.add_argument("--support-export", action="store_true", help="Check support export privacy, scopes, cancellation and failure preservation")
    parser.add_argument("--about", action="store_true", help="Check About navigation, paging and read-only snapshots across scales/orientations")
    parser.add_argument("--about-captures", help="Directory for About framebuffer captures (PGM)")
    parser.add_argument("--sync-server-captures", help="Directory for Sync Server settings captures (PGM)")
    parser.add_argument("--home-themes", action="store_true", help="Compare drawer theme changes with fresh Home renders (X4 Pro)")
    parser.add_argument("--no-build", dest="build", action="store_false", help="Run the existing simulator binary")
    parser.add_argument("--window", dest="headless", action="store_false", help="Show the SDL window instead of using dummy video")
    parser.set_defaults(build=True, headless=True)
    return parser.parse_args()


def main() -> int:
    return run_smoke(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
