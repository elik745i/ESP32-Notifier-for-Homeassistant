from pathlib import Path
import gzip

Import("env")

ROOT = Path(env["PROJECT_DIR"])
WEB_DIR = ROOT / "web"
HEADER = ROOT / "include" / "generated_web_assets.h"
SOURCE = ROOT / "src" / "generated_web_assets.cpp"


def c_array(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        rows.append(", ".join(f"0x{byte:02x}" for byte in chunk))
    return ",\n    ".join(rows)


assets = []
for path in sorted(WEB_DIR.glob("*")):
    if not path.is_file():
        continue
    raw = path.read_bytes()
    compressed = gzip.compress(raw, compresslevel=9)
    mime = {
        ".html": "text/html; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".js": "application/javascript; charset=utf-8",
    }.get(path.suffix.lower(), "application/octet-stream")
    symbol = path.stem.replace("-", "_") + path.suffix.replace(".", "_")
    assets.append((path.name, symbol, mime, compressed))

header_lines = [
    "#pragma once",
    "",
    "#include <Arduino.h>",
    "#include <pgmspace.h>",
    "",
    "struct EmbeddedWebAsset {",
    "    const char* path;",
    "    const char* contentType;",
    "    const uint8_t* data;",
    "    size_t size;",
    "    bool gzip;",
    "};",
    "",
]

source_lines = [
    '#include "generated_web_assets.h"',
    "",
]

for file_name, symbol, _, compressed in assets:
    header_lines.append(f"extern const uint8_t {symbol}[];")
    header_lines.append(f"extern const size_t {symbol}_len;")
    source_lines.append(f"const uint8_t {symbol}[] PROGMEM = {{")
    source_lines.append(f"    {c_array(compressed)}")
    source_lines.append("};")
    source_lines.append(f"const size_t {symbol}_len = sizeof({symbol});")
    source_lines.append("")

header_lines.append("extern const EmbeddedWebAsset WEB_ASSETS[];")
header_lines.append("extern const size_t WEB_ASSET_COUNT;")

source_lines.append("const EmbeddedWebAsset WEB_ASSETS[] = {")
for file_name, symbol, mime, _ in assets:
    source_lines.append(
        f'    {{"/{file_name}", "{mime}", {symbol}, {symbol}_len, true}},'
    )
source_lines.append("};")
source_lines.append("const size_t WEB_ASSET_COUNT = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]);")

HEADER.write_text("\n".join(header_lines) + "\n", encoding="utf-8")
SOURCE.write_text("\n".join(source_lines) + "\n", encoding="utf-8")
