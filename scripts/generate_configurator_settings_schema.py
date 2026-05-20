#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "configurator_schema"
ARDUINOJSON_DIR = ROOT / ".pio" / "libdeps" / "default" / "ArduinoJson" / "src"
EXPORTER = BUILD_DIR / "export_configurator_settings_schema"
OUTPUT = ROOT / "docs" / "configurator" / "settings-schema.generated.js"
JSON_SETTINGS_IO = ROOT / "src" / "JsonSettingsIO.cpp"

SCHEMA_MACROS = [
    "ENABLE_BACKGROUND_SERVER_ON_CHARGE=1",
    "ENABLE_BACKGROUND_SERVER_ALWAYS=1",
    "ENABLE_BOOKERLY_FONTS=1",
    "ENABLE_DARK_MODE=1",
    "ENABLE_FOCUS_READING=1",
    "ENABLE_GLOBAL_STATUS_BAR=1",
    "ENABLE_IMAGE_SLEEP=1",
    "ENABLE_LYRA_THEME=1",
    "ENABLE_MINIMAL_THEME=1",
    "ENABLE_NOTOSANS_FONTS=1",
    "ENABLE_OPENDYSLEXIC_FONTS=1",
    "ENABLE_POKEMON_PARTY=1",
    "ENABLE_READING_STATS=1",
    "ENABLE_ROMAN_CLOCK_SLEEP=1",
    "ENABLE_USB_MASS_STORAGE=1",
    "ENABLE_USER_FONTS=1",
    "ENABLE_WIFI_CLOCK=1",
]

EXCLUDED_PERSISTED_KEYS = {
    "backgroundServerOnCharge",
    "clockFormat",
    "clockHasBeenSynced",
    "clockUtcOffsetQ",
    "frontButtonBack",
    "frontButtonConfirm",
    "frontButtonLayout",
    "frontButtonLeft",
    "frontButtonRight",
    "installedOtaBundle",
    "installedOtaFeatureFlags",
    "language",
    "lastTimeSyncEpoch",
    "opdsPassword_obf",
    "opdsServerUrl",
    "opdsUsername",
    "releaseChannel",
    "sdFontFamilyName",
    "selectedOtaBundle",
    "sleepTimeout",
    "statusBar",
    "statusBarClock",
    "todoFallbackCover",
    "userFontPath",
    "wifiAutoConnect",
}


def ensure_arduinojson() -> None:
    if ARDUINOJSON_DIR.is_dir():
        return
    if subprocess.run(["which", "uv"], capture_output=True, text=True).returncode != 0:
        raise SystemExit("uv is required to bootstrap ArduinoJson")
    subprocess.run(
        ["uv", "run", "pio", "pkg", "install", "-e", "default", "--library", "bblanchon/ArduinoJson@7.4.2"],
        cwd=ROOT,
        check=True,
    )
    if not ARDUINOJSON_DIR.is_dir():
        raise SystemExit(f"ArduinoJson headers not found: {ARDUINOJSON_DIR}")


def build_exporter() -> None:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        "g++",
        "-std=c++20",
        "-O2",
        "-Wno-narrowing",
        "-DCROSSPOINT_HOST_BUILD=1",
        *[f"-D{macro}" for macro in SCHEMA_MACROS],
        f"-I{ROOT}",
        f"-I{ROOT / 'test'}",
        f"-I{ROOT / 'test/mock'}",
        f"-I{ROOT / 'lib/I18n'}",
        f"-I{ROOT / 'lib/EpdFont'}",
        f"-I{ROOT / 'lib/Serialization'}",
        f"-I{ROOT / 'include'}",
        f"-I{ROOT / 'src'}",
        f"-I{ARDUINOJSON_DIR}",
        str(ROOT / "test/tools/export_configurator_settings_schema.cpp"),
        str(ROOT / "src/CrossPointSettings.cpp"),
        str(ROOT / "lib/I18n/I18n.cpp"),
        str(ROOT / "lib/I18n/I18nStrings.cpp"),
        str(ROOT / "test/mock/FeatureModuleHooks.cpp"),
        str(ROOT / "test/mock/JsonSettingsIO.cpp"),
        "-o",
        str(EXPORTER),
    ]
    subprocess.run(cmd, cwd=ROOT, check=True)


def export_schema() -> dict:
    raw = subprocess.check_output([str(EXPORTER)], cwd=ROOT, text=True)
    return json.loads(raw)


def render_asset(schema: dict) -> str:
    payload = json.dumps(schema, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    return f"window.CONFIGURATOR_SETTINGS_SCHEMA = {payload};\n"


def persisted_setting_keys() -> set[str]:
    text = JSON_SETTINGS_IO.read_text(encoding="utf-8")
    match = re.search(r"bool JsonSettingsIO::saveSettings\(.*?\{(.*?)serializeJson\(doc, json\);", text, re.S)
    if not match:
        raise SystemExit("Could not locate JsonSettingsIO::saveSettings block")
    return set(re.findall(r'doc\["([^"]+)"\]', match.group(1)))


def schema_output_keys(schema: dict) -> set[str]:
    keys: set[str] = set()
    for setting in schema["settings"]:
        emits = setting.get("emits")
        if emits:
            for emit in emits:
                keys.add(emit["key"])
            continue
        keys.add(setting["key"])
    return keys


def validate_schema(schema: dict) -> None:
    schema_keys = {setting["key"] for setting in schema["settings"]}
    if len(schema_keys) != len(schema["settings"]):
        raise SystemExit("Schema contains duplicate keys")

    persisted = persisted_setting_keys()
    covered = schema_output_keys(schema)
    excluded = set(EXCLUDED_PERSISTED_KEYS)

    missing = sorted(persisted - covered - excluded)
    if missing:
        raise SystemExit(f"Persisted settings missing from schema coverage/exclusions: {', '.join(missing)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Fail if the generated asset is out of date")
    args = parser.parse_args()

    ensure_arduinojson()
    build_exporter()
    schema = export_schema()
    validate_schema(schema)

    asset = render_asset(schema)
    if args.check:
        existing = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if existing != asset:
            print(f"{OUTPUT} is out of date; run scripts/generate_configurator_settings_schema.py", file=sys.stderr)
            return 1
        return 0

    OUTPUT.write_text(asset, encoding="utf-8")
    print(f"Wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
