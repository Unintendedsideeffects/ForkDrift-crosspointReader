#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path

SESSION = "c0388c"
OUT = Path(__file__).resolve().parents[2] / ".cursor" / f"debug-{SESSION}.log"

LINE_RE = re.compile(
    r"^\[(\d+)\]\s+\[(INF|DBG|WRN|ERR)\]\s+\[([A-Z0-9]+)\]\s+(.*)$"
)


def parse_lines(text: str) -> list[dict]:
    entries = []
    for i, raw in enumerate(text.splitlines(), 1):
        m = LINE_RE.match(raw.strip())
        if not m:
            continue
        ts, level, tag, msg = m.groups()
        entries.append(
            {
                "line": i,
                "ts": int(ts),
                "level": level,
                "tag": tag,
                "msg": msg,
            }
        )
    return entries


def summarize(entries: list[dict]) -> dict:
    def count(sub: str) -> int:
        return sum(1 for e in entries if sub in e["msg"])

    version = next(
        (e["msg"] for e in entries if "Starting CrossPoint version" in e["msg"]),
        None,
    )
    return {
        "lineCount": len(entries),
        "version": version,
        "carouselMallocFailed": count("carousel: malloc failed"),
        "carouselBuiltCache": count("built cache for"),
        "epubLoadOnHome": sum(
            1
            for e in entries
            if e["tag"] == "EBP" and "Loading ePub" in e["msg"]
        ),
        "persistFail": count("Failed to persist page data to SD"),
        "c0388cH1": count("hyp=H1"),
        "c0388cH2": count("hyp=H2"),
        "c0388cH3": count("hyp=H3"),
        "c0388cH4": count("hyp=H4"),
        "c0388cH5": count("hyp=H5"),
        "pslSkip": count("skip give (not holder)"),
        "rdlSkip": count("RDL") and count("skip give"),
        "stlSkip": count("[STL]"),
        "maxGfxMs": max(
            (
                int(m.group(1))
                for e in entries
                for m in [re.search(r"Time = (\d+) ms from clearScreen to displayBuffer", e["msg"])]
                if m
            ),
            default=0,
        ),
        "last10": entries[-10:],
    }


def main() -> int:
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("crosspoint-debug.log")
    if not src.is_file():
        print(f"missing: {src}", file=sys.stderr)
        return 1

    text = src.read_text(encoding="utf-8", errors="replace")
    entries = parse_lines(text)
    summary = summarize(entries)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8") as f:
        f.write(
            json.dumps(
                {
                    "sessionId": SESSION,
                    "hypothesisId": "summary",
                    "location": "parse_crosspoint_debug_log.py",
                    "message": "device log summary",
                    "data": summary,
                    "timestamp": summary["last10"][-1]["ts"] if summary["last10"] else 0,
                }
            )
            + "\n"
        )
        for key in ("c0388cH1", "c0388cH2", "c0388cH3", "c0388cH4", "c0388cH5"):
            if summary[key]:
                hid = key.replace("c0388c", "")
                f.write(
                    json.dumps(
                        {
                            "sessionId": SESSION,
                            "hypothesisId": hid,
                            "location": "crosspoint-debug.log",
                            "message": f"{key} hits",
                            "data": {"count": summary[key]},
                            "timestamp": 0,
                        }
                    )
                    + "\n"
                )

    print(json.dumps(summary, indent=2))
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
