#!/usr/bin/env python3
"""Build and run the ForkDrift simulator smoke test.

Usage:
  python scripts/run_simulator_smoke_test.py [--book PATH] [--theme NAME]
      [--timeout SECS] [--page-turns N] [--no-build] [--window]
      [--fs-root DIR] [--recovery | --sd-fail | --percent-jump]

The smoke test boots the firmware, navigates Home → FileBrowser → RecentBooks →
Settings → Sleep → Reader (with page turns), then exits with code 0 on success.

Boot-flow modes (simulator-only firmware hooks, exit 0 on success):
  --recovery  Boot into the recovery menu and drive it (cache/settings/factory
              reset menu) without confirming destructive actions.
  --sd-fail   Force SD init failure so the SD-missing Safe Mode path runs (the
              real detection is hardware-only; the sim's begin() always succeeds).

By default it runs against a throwaway fs_ directory containing a single copied
book. Pass --fs-root DIR to instead run against a persistent folder you have
prepared (multiple books, a real .crosspoint/ cache, settings.json, etc.). The
folder becomes the simulator's SD root (CROSSPOINT_SIM_SD): the runner neither
seeds nor wipes it, and the firmware reads and writes it exactly like a real SD
card (so caches and reading progress will appear there afterwards). The book to
open is taken from --book if it names an existing path under the tree, otherwise
the first *.epub under DIR/books is used.
"""

from __future__ import annotations

import argparse
import configparser
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOOK = ROOT / "test" / "epubs" / "test_tables.epub"
CRASH_PATTERNS = (
    "std::bad_alloc",
    "terminating due to uncaught exception",
    "Assertion failed",
    "Segmentation fault",
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "SIM OOM",
)
THEMES = {
    "classic": 0,
    "lyra": 1,
    "lyra-extended": 2,
    "lyra_extended": 2,
    "fork-drift": 3,
    "fork_drift": 3,
    "pokemon-party": 4,
    "pokemon_party": 4,
    "minimal": 5,
    "lyra-carousel": 6,
    "lyra_carousel": 6,
    "terminal": 7,
}


def resolve_program() -> Path:
    """Resolve the simulator binary from the same build-dir policy as PlatformIO."""
    candidates: list[Path] = []
    if build_dir := os.environ.get("PLATFORMIO_BUILD_DIR"):
        candidates.append(Path(build_dir).expanduser() / "simulator" / "program")

    local_config = ROOT / "platformio.local.ini"
    if local_config.is_file():
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(local_config)
        if parser.has_option("platformio", "build_dir"):
            configured = parser.get("platformio", "build_dir")
            configured = configured.replace("${sysenv.HOME}", str(Path.home()))
            configured = configured.replace("${PROJECT_DIR}", str(ROOT))
            candidates.append(Path(configured).expanduser() / "simulator" / "program")

    candidates.append(ROOT / ".pio" / "build" / "simulator" / "program")
    return next((candidate for candidate in candidates if candidate.is_file()), candidates[0])


PROGRAM = resolve_program()


def build_simulator() -> None:
    print("Building simulator...", flush=True)
    proc = subprocess.run(["uv", "run", "pio", "run", "-e", "simulator"], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def prepare_fs(temp_root: Path, book: Path) -> str:
    books_dir = temp_root / "fs_" / "books"
    books_dir.mkdir(parents=True, exist_ok=True)
    target = books_dir / book.name
    shutil.copy2(book, target)
    return f"/books/{book.name}"


def resolve_book_in_tree(fs_root: Path, requested: str) -> str | None:
    """Pick the logical SD path of the book to open inside a prepared tree.

    Honors --book when it names an existing file under the tree (given either as
    a logical "/books/x.epub" path or a host path inside fs_root); otherwise
    falls back to the first *.epub found under <fs_root>/books.
    """
    if requested:
        # Logical SD path (e.g. /books/foo.epub) that exists under the root.
        candidate = fs_root / requested.lstrip("/")
        if candidate.is_file():
            rel = candidate.resolve().relative_to(fs_root)
            return "/" + rel.as_posix()
        # Host path that happens to live inside the tree.
        host = Path(requested)
        if host.is_file():
            try:
                rel = host.resolve().relative_to(fs_root)
                return "/" + rel.as_posix()
            except ValueError:
                pass  # Outside the tree — fall through to auto-discovery.

    books_dir = fs_root / "books"
    if books_dir.is_dir():
        for epub in sorted(books_dir.rglob("*.epub")):
            return "/" + epub.relative_to(fs_root).as_posix()
    return None


def base_env(args: argparse.Namespace) -> dict[str, str]:
    env = os.environ.copy()
    env["FORKDRIFT_SIMULATOR_SMOKE_TEST"] = "1"
    env["FORKDRIFT_SIMULATOR_SMOKE_PAGE_TURNS"] = str(args.page_turns)
    if args.theme:
        env["FORKDRIFT_SIMULATOR_SMOKE_THEME"] = str(THEMES[args.theme])
    if args.headless:
        env.setdefault("SDL_VIDEODRIVER", "dummy")
    return env


def run_program(env: dict[str, str], cwd: Path, timeout: int) -> int:
    proc = subprocess.run(
        [str(PROGRAM)],
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
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

    return 0


def run_smoke(args: argparse.Namespace) -> int:
    if args.build:
        build_simulator()

    if not PROGRAM.exists():
        print(f"Simulator binary not found: {PROGRAM}", file=sys.stderr)
        print("Run: uv run pio run -e simulator", file=sys.stderr)
        return 2

    env = base_env(args)

    # Boot-flow modes (simulator-only firmware hooks). Recovery boots straight
    # into the recovery menu; sd-fail forces SD init failure → Safe Mode. Neither
    # opens a book, so the Reader/book steps are skipped.
    if args.recovery:
        env["FORKDRIFT_SIMULATOR_RECOVERY"] = "1"
    if args.sd_fail:
        env["FORKDRIFT_SIMULATOR_SD_FAIL"] = "1"
    if args.percent_jump:
        env["FORKDRIFT_SIMULATOR_SMOKE_PERCENT_JUMP"] = "1"
    if args.controls_options:
        env["FORKDRIFT_SIMULATOR_SMOKE_CONTROLS_OPTIONS"] = "1"
    needs_book = not (args.recovery or args.sd_fail)

    # Persistent, caller-supplied file tree: run the simulator against it in place
    # (CROSSPOINT_SIM_SD), leaving the tree untouched. Useful for exercising real
    # on-disk content — multiple books, a populated .crosspoint/ cache, settings.
    if args.fs_root:
        fs_root = Path(args.fs_root).resolve()
        if not fs_root.is_dir():
            print(f"--fs-root is not a directory: {fs_root}", file=sys.stderr)
            return 2

        if needs_book:
            book_path = resolve_book_in_tree(fs_root, args.book if args.book != str(DEFAULT_BOOK) else "")
            if not book_path:
                print(f"No book to open: pass --book or place a *.epub under {fs_root / 'books'}", file=sys.stderr)
                return 2
            env["FORKDRIFT_SIMULATOR_SMOKE_BOOK"] = book_path

        env["CROSSPOINT_SIM_SD"] = str(fs_root)
        print(f"Running simulator smoke test against fs root: {fs_root}", flush=True)
        return run_program(env, cwd=fs_root, timeout=args.timeout)

    # Default: throwaway isolated fs_ (seeded with a single copied book unless a
    # boot-flow mode that opens no book is selected).
    if not needs_book:
        with tempfile.TemporaryDirectory(prefix="forkdrift-sim-smoke-") as temp_dir_name:
            temp_root = Path(temp_dir_name)
            (temp_root / "fs_" / "books").mkdir(parents=True, exist_ok=True)
            print(f"Running simulator smoke test with isolated fs_: {temp_root / 'fs_'}", flush=True)
            return run_program(env, cwd=temp_root, timeout=args.timeout)

    book = Path(args.book).resolve()
    if not book.exists():
        print(f"Smoke test book not found: {book}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="forkdrift-sim-smoke-") as temp_dir_name:
        temp_root = Path(temp_dir_name)
        env["FORKDRIFT_SIMULATOR_SMOKE_BOOK"] = prepare_fs(temp_root, book)
        print(f"Running simulator smoke test with isolated fs_: {temp_root / 'fs_'}", flush=True)
        return run_program(env, cwd=temp_root, timeout=args.timeout)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--book", default=str(DEFAULT_BOOK),
                        help="EPUB to copy into the isolated simulator fs_")
    parser.add_argument("--timeout", type=int, default=60,
                        help="Seconds before the simulator run is treated as hung")
    parser.add_argument("--page-turns", type=int, default=2,
                        help="Number of page-forward taps to run in the reader")
    parser.add_argument("--theme", choices=sorted(THEMES),
                        help="UI theme to use during the smoke test")
    parser.add_argument("--no-build", dest="build", action="store_false",
                        help="Skip build; run the existing simulator binary")
    parser.add_argument("--window", dest="headless", action="store_false",
                        help="Show the SDL window instead of using dummy video")
    parser.add_argument("--fs-root", default=None,
                        help="Run against this folder as the simulator SD root (CROSSPOINT_SIM_SD) "
                             "instead of a throwaway fs_; the runner does not seed or wipe it")
    parser.add_argument("--recovery", action="store_true",
                        help="Boot straight into the recovery menu and drive it (no book opened)")
    parser.add_argument("--sd-fail", action="store_true",
                        help="Force SD init failure to exercise the Safe Mode path (no book opened)")
    parser.add_argument("--percent-jump", action="store_true",
                        help="Drive reader menu -> percent selection -> jump (~70%%) and require a settled frame")
    parser.add_argument("--controls-options", action="store_true",
                        help="Drive reader menu -> Controls options -> select row")
    parser.set_defaults(build=True, headless=True)
    return parser.parse_args()


def main() -> int:
    return run_smoke(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
