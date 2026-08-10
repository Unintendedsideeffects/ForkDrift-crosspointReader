"""Every settings key the Settings screen asks for must be emitted under the same
compile-time gate.

Regression: SettingsActivity requested "ankiConnectUrl"/"ankiConnectDeck"
unconditionally, but SettingsList.h only emits them under `#if
ENABLE_ANKI_SUPPORT`. Any anki-disabled build therefore logged two
`[ERR] [SET] Missing connect setting definition` lines on every entry to
Settings, and rendered an empty Anki Connect section header.

The rule this enforces: if the producer emits a key inside `#if FLAG`, the
consumer that looks the key up must sit inside `#if FLAG` too.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRODUCER = ROOT / "src" / "SettingsList.h"
CONSUMER = ROOT / "src" / "activities" / "settings" / "SettingsActivity.cpp"

FLAG_RE = re.compile(r"\bENABLE_[A-Z0-9_]+\b")
LOOKUP_RE = re.compile(r"add\w*SettingByKey\(\"([A-Za-z0-9_]+)\"\)")


def gates_by_line(path: Path) -> list[frozenset[str]]:
    """For each line, the set of ENABLE_* flags whose #if block encloses it.

    Negated conditions (`#if !ENABLE_X`) are deliberately ignored: they gate the
    fallback branch, not the feature, so they carry no obligation.
    """
    stack: list[frozenset[str]] = []
    out: list[frozenset[str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("#if"):
            positive = "!" not in stripped
            stack.append(frozenset(FLAG_RE.findall(stripped)) if positive else frozenset())
        elif stripped.startswith("#else") or stripped.startswith("#elif"):
            if stack:
                stack[-1] = frozenset()
        elif stripped.startswith("#endif"):
            if stack:
                stack.pop()
        out.append(frozenset().union(*stack) if stack else frozenset())
    return out


def producer_gates() -> dict[str, frozenset[str]]:
    """Gate set for each key literal emitted in SettingsList.h."""
    gates = gates_by_line(PRODUCER)
    found: dict[str, frozenset[str]] = {}
    for lineno, line in enumerate(PRODUCER.read_text(encoding="utf-8").splitlines()):
        for key in re.findall(r'"([A-Za-z0-9_]+)"', line):
            # A key may be emitted more than once (e.g. the split sleep-screen
            # variants); the weakest gate is the one that decides availability.
            if key in found:
                found[key] = found[key] & gates[lineno]
            else:
                found[key] = gates[lineno]
    return found


def test_every_looked_up_settings_key_shares_its_producer_gate():
    produced = producer_gates()
    consumer_lines = CONSUMER.read_text(encoding="utf-8").splitlines()
    consumer_gates = gates_by_line(CONSUMER)

    violations = []
    for lineno, line in enumerate(consumer_lines):
        for key in LOOKUP_RE.findall(line):
            required = produced.get(key)
            assert required is not None, (
                f"{CONSUMER.name}:{lineno + 1} looks up {key!r}, which "
                f"{PRODUCER.name} never emits"
            )
            missing = required - consumer_gates[lineno]
            if missing:
                violations.append(
                    f"{CONSUMER.name}:{lineno + 1}: {key!r} is emitted only under "
                    f"{sorted(required)} but is looked up without {sorted(missing)}"
                )

    assert not violations, "\n".join(violations)
