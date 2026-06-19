#!/usr/bin/env python3
"""
Generate /dictionary/en.dict for the in-reader dictionary feature (plan 016/018).

Produces a UTF-8 file of `headword<TAB>definition` lines, sorted by the EXACT key the
firmware uses in src/util/DictionaryLookup.cpp: case-fold ASCII A-Z->a-z only (no Unicode,
no locale), compared as raw bytes. The firmware does a seek-based block binary search that
DEPENDS on this ordering — any other sort (str.lower(), locale collation) breaks lookups
silently. The --check self-test guards that contract.

Source: a public-domain English dictionary as JSON {headword: definition} (default: Webster's
1913) or TSV (headword<TAB>definition per line). Swap with --source.

Usage:
    python scripts/generate_dictionary.py --source /tmp/webster.json --out dist/dictionary/en.dict
    python scripts/generate_dictionary.py --source /tmp/webster.json --sample 200   # quick on-device smoke dict
    python scripts/generate_dictionary.py --check dist/dictionary/en.dict           # verify sort contract
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def casefold_ascii(s: str) -> str:
    """Mirror DictionaryLookup::caseFold EXACTLY: only ASCII A-Z -> a-z."""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def sort_key(headword: str) -> bytes:
    """The firmware compares folded headwords with std::string::compare (unsigned byte
    lexicographic). Python sorts bytes the same way, so encode the folded key to bytes."""
    return casefold_ascii(headword).encode("utf-8")


def normalize_def(text: str, max_chars: int) -> str:
    """One line, no tabs/newlines (def is everything after the first tab in the file)."""
    out = " ".join(text.split())  # collapses all whitespace incl. \n \r \t
    if max_chars > 0 and len(out) > max_chars:
        out = out[: max_chars - 1].rstrip() + "…"
    return out


def load_entries(source: Path) -> list[tuple[str, str]]:
    raw = source.read_text(encoding="utf-8")
    entries: list[tuple[str, str]] = []
    if source.suffix.lower() == ".json":
        data = json.loads(raw)
        if not isinstance(data, dict):
            raise SystemExit("JSON source must be an object {headword: definition}")
        entries = list(data.items())
    else:  # TSV
        for line in raw.splitlines():
            if not line.strip():
                continue
            head, _, definition = line.partition("\t")
            entries.append((head, definition))
    return entries


def build(entries: list[tuple[str, str]], max_def: int, sample: int) -> list[tuple[str, str, bytes]]:
    rows: list[tuple[str, str, bytes]] = []
    for head, definition in entries:
        head = head.strip()
        if not head or "\t" in head or "\n" in head:
            continue
        # Drop letterless artifact headwords (e.g. "-", "--") but keep real suffix
        # entries like "-able" that legitimately start with punctuation.
        if not any("a" <= c <= "z" or "A" <= c <= "Z" for c in head):
            continue
        d = normalize_def(str(definition), max_def)
        if not d:
            continue
        rows.append((head, d, sort_key(head)))
    rows.sort(key=lambda r: r[2])
    if sample > 0:
        rows = rows[:sample]
    return rows


def check(path: Path) -> int:
    prev = b""
    n = 0
    with path.open("r", encoding="utf-8") as fh:
        for ln, line in enumerate(fh, 1):
            line = line.rstrip("\n")
            head = line.split("\t", 1)[0]
            key = sort_key(head)
            if key < prev:
                print(f"SORT CONTRACT VIOLATION at line {ln}: {head!r} is out of order", file=sys.stderr)
                return 1
            prev = key
            n += 1
    print(f"OK: {n} entries sorted by the firmware case-fold-ASCII byte key")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate en.dict for the in-reader dictionary.")
    ap.add_argument("--source", type=Path, help="JSON {word:def} or TSV source")
    ap.add_argument("--out", type=Path, default=Path("dist/dictionary/en.dict"))
    ap.add_argument("--sample", type=int, default=0, help="emit only the first N (sorted) entries")
    ap.add_argument("--max-def-chars", type=int, default=4000, help="truncate definitions longer than this (0=off)")
    ap.add_argument("--check", type=Path, help="verify an existing en.dict obeys the sort contract; no generation")
    args = ap.parse_args()

    if args.check:
        return check(args.check)
    if not args.source:
        ap.error("--source is required (or use --check)")

    rows = build(load_entries(args.source), args.max_def_chars, args.sample)
    if not rows:
        raise SystemExit("no entries produced")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as fh:
        for head, d, _ in rows:
            fh.write(f"{head}\t{d}\n")
    print(f"Wrote {args.out} ({len(rows)} entries, {args.out.stat().st_size} bytes)")
    rc = check(args.out)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
