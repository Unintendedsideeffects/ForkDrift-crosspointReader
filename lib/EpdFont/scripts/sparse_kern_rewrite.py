#!/usr/bin/env python3
"""
Rewrite builtin font headers from packed+dense kerning to split class maps + CSR.

Parses each header's KernLeftClasses / KernRightClasses / KernMatrix, emits
aligned uint16/uint8 class arrays and a CSR (row offsets, columns, values),
drops the dense matrix, and inserts the seven new EpdFontData pointer slots.
Glyph bitmaps and metrics are left untouched.

--verify reloads the original dense tables via `git show HEAD:<file>` and
asserts every left-class x right-class pair matches the rewritten sparse form.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

PACKED_CLASS_RE = re.compile(
    r"static const EpdKernClassEntry\s+(\w+)Kern(Left|Right)Classes\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
MATRIX_RE = re.compile(
    r"static const int8_t\s+(\w+)KernMatrix\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
SPLIT_CPS_RE = re.compile(
    r"static const uint16_t\s+(\w+)Kern(Left|Right)Codepoints\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
SPLIT_IDS_RE = re.compile(
    r"static const uint8_t\s+(\w+)Kern(Left|Right)ClassIds\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
ROW_OFFSETS_RE = re.compile(
    r"static const uint16_t\s+(\w+)KernRowOffsets\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
SPARSE_COLS_RE = re.compile(
    r"static const uint8_t\s+(\w+)KernSparseCols\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
SPARSE_VALS_RE = re.compile(
    r"static const int8_t\s+(\w+)KernSparseValues\[\]\s*=\s*\{(.*?)\};",
    re.S,
)
PACKED_ENTRY_RE = re.compile(r"\{\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*\}")
HEX_RE = re.compile(r"0x([0-9A-Fa-f]+)")
INT_RE = re.compile(r"-?\d+")
DENSE_COUNTS_RE = re.compile(
    r"(\w+)KernMatrix,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),",
)
SPARSE_COUNTS_RE = re.compile(
    r"(\w+)KernSparseValues,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),",
)
DENSE_PTRS_RE = re.compile(
    r"    \w+KernLeftClasses,\n    \w+KernRightClasses,\n    \w+KernMatrix,",
)
DEFAULT_FAMILIES = ("ubuntu_", "lexenddeca_", "charein_")


def chunks(values, n):
    for i in range(0, len(values), n):
        yield values[i : i + n]


def format_array(ctype, name, values, formatter, per_line):
    lines = [f"static const {ctype} {name}[] = {{"]
    for chunk in chunks(values, per_line):
        lines.append("    " + ", ".join(formatter(v) for v in chunk) + ",")
    lines.append("};\n")
    return "\n".join(lines)


def parse_packed_classes(text, side):
    matches = [m for m in PACKED_CLASS_RE.finditer(text) if m.group(2) == side]
    if len(matches) != 1:
        return None
    entries = [(int(cp, 16), int(cls)) for cp, cls in PACKED_ENTRY_RE.findall(matches[0].group(3))]
    return matches[0].group(1), entries, matches[0]


def parse_int_array(body):
    return [int(v) for v in INT_RE.findall(body)]


def parse_hex_array(body):
    return [int(v, 16) for v in HEX_RE.findall(body)]


def parse_dense(text):
    left = parse_packed_classes(text, "Left")
    right = parse_packed_classes(text, "Right")
    matrix_match = MATRIX_RE.search(text)
    counts_match = DENSE_COUNTS_RE.search(text)
    if not left or not right or not matrix_match or not counts_match:
        return None
    name, left_entries, _ = left
    _, right_entries, _ = right
    if matrix_match.group(1) != name or counts_match.group(1) != name:
        return None
    matrix = parse_int_array(matrix_match.group(2))
    left_n, right_n, left_c, right_c = (int(counts_match.group(i)) for i in range(2, 6))
    return {
        "name": name,
        "left": left_entries,
        "right": right_entries,
        "matrix": matrix,
        "left_n": left_n,
        "right_n": right_n,
        "left_c": left_c,
        "right_c": right_c,
        "left_span": left[2].span(),
        "matrix_span": matrix_match.span(),
        "counts_match": counts_match,
    }


def parse_sparse(text):
    left_cps = {m.group(1): parse_hex_array(m.group(3)) for m in SPLIT_CPS_RE.finditer(text) if m.group(2) == "Left"}
    right_cps = {m.group(1): parse_hex_array(m.group(3)) for m in SPLIT_CPS_RE.finditer(text) if m.group(2) == "Right"}
    left_ids = {m.group(1): parse_int_array(m.group(3)) for m in SPLIT_IDS_RE.finditer(text) if m.group(2) == "Left"}
    right_ids = {m.group(1): parse_int_array(m.group(3)) for m in SPLIT_IDS_RE.finditer(text) if m.group(2) == "Right"}
    offsets = {m.group(1): parse_int_array(m.group(2)) for m in ROW_OFFSETS_RE.finditer(text)}
    cols = {m.group(1): parse_int_array(m.group(2)) for m in SPARSE_COLS_RE.finditer(text)}
    vals = {m.group(1): parse_int_array(m.group(2)) for m in SPARSE_VALS_RE.finditer(text)}
    counts_match = SPARSE_COUNTS_RE.search(text)
    if not counts_match:
        return None
    name = counts_match.group(1)
    if name not in left_cps or name not in right_cps or name not in left_ids or name not in right_ids:
        return None
    if name not in offsets or name not in cols or name not in vals:
        return None
    left_n, right_n, left_c, right_c = (int(counts_match.group(i)) for i in range(2, 6))
    return {
        "name": name,
        "left": list(zip(left_cps[name], left_ids[name])),
        "right": list(zip(right_cps[name], right_ids[name])),
        "offsets": offsets[name],
        "cols": cols[name],
        "vals": vals[name],
        "left_n": left_n,
        "right_n": right_n,
        "left_c": left_c,
        "right_c": right_c,
    }


def dense_to_csr(matrix, left_c, right_c):
    offsets = []
    cols = []
    vals = []
    for row in range(left_c):
        offsets.append(len(cols))
        start = row * right_c
        for col, v in enumerate(matrix[start : start + right_c]):
            if v != 0:
                cols.append(col)
                vals.append(v)
    offsets.append(len(cols))
    return offsets, cols, vals


def csr_lookup(offsets, cols, vals, lc, rc):
    begin = offsets[lc - 1]
    end = offsets[lc]
    target = rc - 1
    for i in range(begin, end):
        if cols[i] == target:
            return vals[i]
        if cols[i] > target:
            break
    return 0


def dense_lookup(matrix, right_c, lc, rc):
    return matrix[(lc - 1) * right_c + (rc - 1)]


def byte_delta(left_c, right_c, nnz):
    dense = left_c * right_c
    sparse = 2 * (left_c + 1) + 2 * nnz
    return dense, sparse, sparse - dense


def validate_dense(tables, path):
    name = tables["name"]
    if len(tables["left"]) != tables["left_n"]:
        raise SystemExit(f"{path}: left class entries {len(tables['left'])} != count {tables['left_n']}")
    if len(tables["right"]) != tables["right_n"]:
        raise SystemExit(f"{path}: right class entries {len(tables['right'])} != count {tables['right_n']}")
    expected = tables["left_c"] * tables["right_c"]
    if len(tables["matrix"]) != expected:
        raise SystemExit(f"{path}: matrix length {len(tables['matrix'])} != {tables['left_c']}*{tables['right_c']}")
    max_left = max((cls for _, cls in tables["left"]), default=0)
    max_right = max((cls for _, cls in tables["right"]), default=0)
    if max_left != tables["left_c"] or max_right != tables["right_c"]:
        raise SystemExit(
            f"{path}: class-id max L{max_left}/R{max_right} != counts {tables['left_c']}/{tables['right_c']}"
        )
    if tables["right_c"] > 256:
        raise SystemExit(f"{path}: {tables['right_c']} right classes exceed uint8 column range")
    return name


def emit_sparse_arrays(name, left, right, offsets, cols, vals):
    parts = [
        format_array("uint16_t", f"{name}KernLeftCodepoints", [cp for cp, _ in left], lambda v: f"0x{v:04X}", 12),
        format_array("uint8_t", f"{name}KernLeftClassIds", [cls for _, cls in left], lambda v: f"{v:3d}", 16),
        format_array("uint16_t", f"{name}KernRightCodepoints", [cp for cp, _ in right], lambda v: f"0x{v:04X}", 12),
        format_array("uint8_t", f"{name}KernRightClassIds", [cls for _, cls in right], lambda v: f"{v:3d}", 16),
        format_array("uint16_t", f"{name}KernRowOffsets", offsets, lambda v: f"{v:5d}", 16),
        format_array("uint8_t", f"{name}KernSparseCols", cols, lambda v: f"{v:3d}", 16),
        format_array("int8_t", f"{name}KernSparseValues", vals, lambda v: f"{v:4d}", 16),
    ]
    return "\n".join(parts)


def rewrite_header(path: Path):
    text = path.read_text(encoding="utf-8")
    if "KernLeftCodepoints" in text:
        raise SystemExit(f"{path}: already rewritten (KernLeftCodepoints present)")
    tables = parse_dense(text)
    if not tables:
        raise SystemExit(f"{path}: missing packed KernLeftClasses / KernRightClasses / KernMatrix")
    name = validate_dense(tables, path)
    if len(tables["matrix"]) > 0 and tables["right_c"] == 0:
        raise SystemExit(f"{path}: matrix present with zero right classes")
    offsets, cols, vals = dense_to_csr(tables["matrix"], tables["left_c"], tables["right_c"])
    if len(cols) > 0xFFFF:
        raise SystemExit(f"{path}: {len(cols)} kern entries exceed the uint16 row-offset range")
    replacement = emit_sparse_arrays(name, tables["left"], tables["right"], offsets, cols, vals)
    start = tables["left_span"][0]
    end = tables["matrix_span"][1]
    text = text[:start] + replacement + text[end:]
    ptrs = (
        "    nullptr,\n"
        "    nullptr,\n"
        f"    {name}KernLeftCodepoints,\n"
        f"    {name}KernLeftClassIds,\n"
        f"    {name}KernRightCodepoints,\n"
        f"    {name}KernRightClassIds,\n"
        "    nullptr,\n"
        f"    {name}KernRowOffsets,\n"
        f"    {name}KernSparseCols,\n"
        f"    {name}KernSparseValues,"
    )
    text, n = DENSE_PTRS_RE.subn(ptrs, text, count=1)
    if n != 1:
        raise SystemExit(f"{path}: failed to insert the seven EpdFontData kern slots")
    path.write_text(text, encoding="utf-8")
    dense_b, sparse_b, delta = byte_delta(tables["left_c"], tables["right_c"], len(cols))
    class_b = 3 * (tables["left_n"] + tables["right_n"])
    return {
        "name": name,
        "path": path,
        "pairs": tables["left_c"] * tables["right_c"],
        "nnz": len(cols),
        "dense_b": dense_b,
        "sparse_b": sparse_b,
        "delta": delta,
        "class_b": class_b,
    }


def git_show_head(relpath: str, repo: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), "show", f"HEAD:{relpath}"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"git show HEAD:{relpath} failed: {result.stderr.strip()}")
    return result.stdout


def verify_header(path: Path, repo: Path):
    relpath = path.relative_to(repo).as_posix()
    original = parse_dense(git_show_head(relpath, repo))
    if not original:
        raise SystemExit(f"{path}: HEAD revision is not packed+dense")
    validate_dense(original, f"HEAD:{relpath}")
    rewritten = parse_sparse(path.read_text(encoding="utf-8"))
    if not rewritten:
        raise SystemExit(f"{path}: rewritten file is not split+CSR")
    if original["name"] != rewritten["name"]:
        raise SystemExit(f"{path}: font name mismatch {original['name']} vs {rewritten['name']}")
    for key in ("left_n", "right_n", "left_c", "right_c"):
        if original[key] != rewritten[key]:
            raise SystemExit(f"{path}: {key} {original[key]} != {rewritten[key]}")
    if original["left"] != rewritten["left"] or original["right"] != rewritten["right"]:
        raise SystemExit(f"{path}: split class maps do not match packed maps")
    expected_off, expected_cols, expected_vals = dense_to_csr(
        original["matrix"], original["left_c"], original["right_c"]
    )
    if rewritten["offsets"] != expected_off or rewritten["cols"] != expected_cols or rewritten["vals"] != expected_vals:
        raise SystemExit(f"{path}: CSR tables do not match dense-to-sparse conversion")
    mismatches = 0
    pairs = original["left_c"] * original["right_c"]
    for lc in range(1, original["left_c"] + 1):
        for rc in range(1, original["right_c"] + 1):
            dense_v = dense_lookup(original["matrix"], original["right_c"], lc, rc)
            sparse_v = csr_lookup(rewritten["offsets"], rewritten["cols"], rewritten["vals"], lc, rc)
            if dense_v != sparse_v:
                mismatches += 1
                if mismatches <= 8:
                    print(f"{path}: mismatch class ({lc},{rc}) dense={dense_v} sparse={sparse_v}", file=sys.stderr)
    if mismatches:
        raise SystemExit(f"{path}: {mismatches} of {pairs} class pairs differ")
    dense_b, sparse_b, delta = byte_delta(original["left_c"], original["right_c"], len(rewritten["cols"]))
    return {
        "name": original["name"],
        "path": path,
        "pairs": pairs,
        "nnz": len(rewritten["cols"]),
        "dense_b": dense_b,
        "sparse_b": sparse_b,
        "delta": delta,
        "class_b": 3 * (original["left_n"] + original["right_n"]),
        "mismatches": mismatches,
    }


def list_headers(fonts_dir: Path):
    headers = sorted(p for p in fonts_dir.glob("*.h") if p.name != "all.h")
    if not headers:
        raise SystemExit(f"no font headers in {fonts_dir}")
    return headers


def print_stats(rows, title):
    print(title)
    default_dense = default_sparse = default_delta = 0
    all_dense = all_sparse = all_delta = 0
    all_pairs = 0
    default_files = 0
    for row in rows:
        all_dense += row["dense_b"]
        all_sparse += row["sparse_b"]
        all_delta += row["delta"]
        all_pairs += row["pairs"]
        stem = row["path"].name
        is_default = stem.startswith(DEFAULT_FAMILIES)
        mark = " default" if is_default else ""
        if is_default:
            default_dense += row["dense_b"]
            default_sparse += row["sparse_b"]
            default_delta += row["delta"]
            default_files += 1
        print(
            f"  {stem}: {row['pairs']} pairs, {row['nnz']} nnz, "
            f"matrix {row['dense_b']} -> {row['sparse_b']} ({row['delta']:+d} B), "
            f"class maps {row['class_b']} B{mark}"
        )
    print(
        f"ALL {len(rows)} headers: {all_pairs} class pairs, "
        f"matrix {all_dense} -> {all_sparse} ({all_delta:+d} B)"
    )
    print(
        f"DEFAULT ubuntu+lexenddeca+charein ({default_files} headers): "
        f"matrix {default_dense} -> {default_sparse} ({default_delta:+d} B)"
    )
    return default_dense, default_sparse, default_delta, all_pairs


def repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "fonts_dir",
        nargs="?",
        default=None,
        help="Directory of builtin *.h fonts (default: ../builtinFonts relative to this script)",
    )
    parser.add_argument("--verify", action="store_true", help="Compare rewritten headers against git HEAD dense tables")
    args = parser.parse_args()
    script_dir = Path(__file__).resolve().parent
    fonts_dir = Path(args.fonts_dir) if args.fonts_dir else script_dir.parent / "builtinFonts"
    fonts_dir = fonts_dir.resolve()
    headers = list_headers(fonts_dir)
    repo = repo_root()
    if args.verify:
        rows = [verify_header(path, repo) for path in headers]
        _, _, _, pairs = print_stats(rows, "VERIFY")
        print(f"VERIFY OK: {len(rows)} headers, {pairs} class pairs, 0 mismatches")
        return 0
    rows = [rewrite_header(path) for path in headers]
    print_stats(rows, "REWRITE")
    print(f"REWRITE OK: {len(rows)} headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
