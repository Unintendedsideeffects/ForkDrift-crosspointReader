import os
import re
import gzip
import json
from pathlib import Path

SRC_DIR = "src"
POKEMON_CACHE_HTML_NAMES = ("PokemonWallpaperPluginPage.html", "PokemonPartyPluginPage.html")
POKEDEX_CACHE_MARKER_RE = re.compile(
    r"/\* POKEMON_CACHE_INJECT_START \*/.*?/\* POKEMON_CACHE_INJECT_END \*/",
    re.DOTALL,
)
THEME_TOKENS_MARKER_RE = re.compile(
    r"/\* THEME_TOKENS_START \*/.*?/\* THEME_TOKENS_END \*/",
    re.DOTALL,
)


def load_theme_tokens(script_dir: str) -> str:
    """Read theme.css, extract body of the :root { } block."""
    theme_path = Path(script_dir) / "theme.css"
    text = theme_path.read_text(encoding="utf-8")
    m = re.search(r":root\s*\{([^}]*)\}", text, re.DOTALL)
    if not m:
        raise RuntimeError(f"No :root block found in {theme_path}")
    return m.group(1)


def inject_theme_tokens(html: str, tokens: str) -> str:
    """Replace THEME_TOKENS markers with canonical token declarations."""
    if "THEME_TOKENS_START" not in html:
        return html  # page opted out — no markers
    if not THEME_TOKENS_MARKER_RE.search(html):
        raise RuntimeError("THEME_TOKENS_START found but marker pair not matched — malformed markers")
    replacement = f"/* THEME_TOKENS_START */{tokens}    /* THEME_TOKENS_END */"
    return THEME_TOKENS_MARKER_RE.sub(replacement, html)


def update_configurator_tokens(tokens: str, repo_root: str):
    """Inject theme tokens into buildPreviewDoc() in the configurator."""
    path = Path(repo_root) / "docs/configurator/index.html"
    if not path.exists():
        return
    content = path.read_text(encoding="utf-8")
    updated = inject_theme_tokens(content, tokens)
    if updated != content:
        path.write_text(updated, encoding="utf-8")
        print(f"Updated theme tokens in {path}")


def inject_pokemon_cache(html_path: str, html: str) -> str:
    path = Path(html_path)
    if path.name not in POKEMON_CACHE_HTML_NAMES:
        return html

    cache_path = path.parent / "pokemon.cache.json"
    if not cache_path.exists():
        return html

    with open(cache_path, "r", encoding="utf-8") as cache_file:
        cache_data = json.load(cache_file)

    if "POKEMON_CACHE_INJECT_START" not in html:
        raise RuntimeError(f"Missing Pokemon cache marker in {html_path}")

    json_str = json.dumps(cache_data, separators=(",", ":"), ensure_ascii=False)
    json_str = json_str.replace("</", "<\\/")
    replacement = f"/* POKEMON_CACHE_INJECT_START */ {json_str} /* POKEMON_CACHE_INJECT_END */"
    injected = POKEDEX_CACHE_MARKER_RE.sub(replacement, html)

    if injected == html:
        raise RuntimeError(f"Failed to inject Pokemon cache from {cache_path}")

    print(f"Injected Pokemon cache from {cache_path} ({len(cache_data)} entries)")
    return injected

def minify_html(html: str) -> str:
    # Tags where whitespace should be preserved
    preserve_tags = ['pre', 'code', 'textarea', 'script', 'style']
    preserve_regex = '|'.join(preserve_tags)

    # Protect preserve blocks with placeholders
    preserve_blocks = []
    def preserve(match):
        preserve_blocks.append(match.group(0))
        return f"__PRESERVE_BLOCK_{len(preserve_blocks)-1}__"

    html = re.sub(rf'<({preserve_regex})[\s\S]*?</\1>', preserve, html, flags=re.IGNORECASE)

    # Remove HTML comments
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL)

    # Collapse all whitespace between tags
    html = re.sub(r'>\s+<', '><', html)

    # Collapse multiple spaces inside tags
    html = re.sub(r'\s+', ' ', html)

    # Restore preserved blocks
    for i, block in enumerate(preserve_blocks):
        html = html.replace(f"__PRESERVE_BLOCK_{i}__", block)

    return html.strip()

def sanitize_identifier(name: str) -> str:
    """Sanitize a filename to create a valid C identifier.

    C identifiers must:
    - Start with a letter or underscore
    - Contain only letters, digits, and underscores
    """
    # Replace non-alphanumeric characters (including hyphens) with underscores
    sanitized = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    # Prefix with underscore if starts with a digit
    if sanitized and sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized


def resolve_script_dir() -> str:
    script_file = globals().get("__file__")
    if script_file:
        return os.path.dirname(os.path.abspath(script_file))

    scons_import = globals().get("Import")
    if callable(scons_import):
        scons_import("env")
        project_dir = env.get("PROJECT_DIR") or env.subst("$PROJECT_DIR")
        if project_dir:
            return str(Path(project_dir) / "scripts")

    return os.path.join(os.getcwd(), "scripts")


SCRIPT_DIR = resolve_script_dir()
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
theme_tokens = load_theme_tokens(SCRIPT_DIR)

for root, _, files in os.walk(SRC_DIR):
    for file in files:
        if file.endswith(".html") or file.endswith(".js"):
            file_path = os.path.join(root, file)
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()

            # Only minify and inject for HTML files; JS files are typically pre-minified (e.g., jszip.min.js)
            if file.endswith(".html"):
                content = inject_pokemon_cache(file_path, content)
                content = inject_theme_tokens(content, theme_tokens)
                processed = minify_html(content)
            else:
                processed = content

            # Compress with gzip (compresslevel 9 is maximum compression)
            # IMPORTANT: we don't use brotli because Firefox doesn't support brotli with insecured context (only supported on HTTPS)
            # mtime=0 removes timestamp entropy and OS byte is pinned to keep output deterministic across Python versions/platforms.
            compressed = bytearray(gzip.compress(processed.encode('utf-8'), compresslevel=9, mtime=0))
            if len(compressed) > 9:
                compressed[9] = 0xFF
            compressed = bytes(compressed)

            # Create valid C identifier from filename
            # Use appropriate suffix based on file type
            suffix = "Html" if file.endswith(".html") else "Js"
            base_name = sanitize_identifier(f"{os.path.splitext(file)[0]}{suffix}")
            header_path = os.path.join(root, f"{base_name}.generated.h")

            with open(header_path, "w", encoding="utf-8") as h:
                h.write(f"// THIS FILE IS AUTOGENERATED, DO NOT EDIT MANUALLY\n\n")
                h.write(f"#pragma once\n")
                h.write(f"#include <cstddef>\n\n")

                # Write the compressed data as a byte array.
                # clang-format off/on guards prevent it from repacking the rows.
                h.write(f"constexpr char {base_name}[] PROGMEM = {{\n")
                h.write(f"  // clang-format off\n")

                # Write bytes in rows of 16
                for i in range(0, len(compressed), 16):
                    chunk = compressed[i:i+16]
                    hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
                    h.write(f"  {hex_values},\n")

                h.write(f"  // clang-format on\n")
                h.write(f"}};\n\n")
                h.write(f"constexpr size_t {base_name}CompressedSize = {len(compressed)};\n")
                h.write(f"constexpr size_t {base_name}OriginalSize = {len(processed)};\n")

            print(f"Generated: {header_path}")
            print(f"  Original: {len(content)} bytes")
            print(f"  Minified: {len(processed)} bytes ({100*len(processed)/len(content):.1f}%)")
            print(f"  Compressed: {len(compressed)} bytes ({100*len(compressed)/len(content):.1f}%)")

update_configurator_tokens(theme_tokens, REPO_ROOT)
