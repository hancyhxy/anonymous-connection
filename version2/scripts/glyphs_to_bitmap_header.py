#!/usr/bin/env python3
"""
Generate firmware/serial_avatar/glyphs.h — a compile-time glyph bitmap table.

Why this exists
---------------
Rendering UTF-8 sprites through the U8g2 font path on ESP32-C6 + Arduino_GFX
1.6.5 was flaky — the 317 KB `u8g2_font_unifont_h_chinese4` data dragged down
flash usage to 52% and some BMP geometric glyphs (▰ ◎ ◠ …) rendered as blank
cells at runtime. Baud-rate / flash-chip timing issues on re-flash made the
problem hard to iterate on.

Replacing the font path with a *precomputed* 8x16 bitmap lookup gives us:
  - ~1.7-3.2 KB of PROGMEM data instead of 317 KB
  - zero runtime dependency on U8g2
  - deterministic rendering (a codepoint either IS in the table or ISN'T —
    no silent "font missing glyph" cases)

Coverage (plan Y)
-----------------
1. Every unique non-space UTF-8 codepoint used in web/sprites.json.
2. All printable ASCII (U+0020 .. U+007E), for quote-bubble text.

Both pull pixel data from the same source as u8g2_font_unifont_h_chinese4:
GNU Unifont's canonical `.hex` dump. So visual output matches what the
font-based rendering was *trying* to produce.

Usage
-----
    python3 scripts/glyphs_to_bitmap_header.py

Requires only the stdlib; downloads unifont.hex on first run and caches it
under scripts/.cache/.
"""

from __future__ import annotations

import json
import os
import sys
import urllib.request
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPRITES_JSON = ROOT / "web" / "sprites.json"
HEADER_OUT = ROOT / "firmware" / "serial_avatar" / "glyphs.h"
CACHE_DIR = ROOT / "scripts" / ".cache"
UNIFONT_URL = "https://unifoundry.com/pub/unifont/unifont-16.0.02/font-builds/unifont-16.0.02.hex.gz"
UNIFONT_HEX = CACHE_DIR / "unifont-16.0.02.hex"


# ---------------------------------------------------------------------------
# Unifont loader
# ---------------------------------------------------------------------------

def download_unifont() -> None:
    """Fetch unifont-X.Y.Z.hex.gz and unpack it into the scripts cache.

    The .hex.gz distribution is ~1.4 MB and contains one line per glyph:
        NNNN:HHHH...HHHH
    where NNNN is the hex codepoint and HHHH... is the packed bitmap
    (16 hex chars = 8x16 px half-width, 32 hex chars = 16x16 px full-width).
    """
    import gzip
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    gz_path = CACHE_DIR / "unifont.hex.gz"
    if UNIFONT_HEX.exists():
        return
    print(f"[glyphs] downloading unifont from {UNIFONT_URL}")
    urllib.request.urlretrieve(UNIFONT_URL, gz_path)
    print(f"[glyphs] unpacking {gz_path}")
    with gzip.open(gz_path, "rb") as src, open(UNIFONT_HEX, "wb") as dst:
        dst.write(src.read())
    gz_path.unlink()


def load_unifont() -> dict[int, str]:
    """Return {codepoint: hex_bitmap_string} from unifont.hex."""
    if not UNIFONT_HEX.exists():
        download_unifont()
    table: dict[int, str] = {}
    with open(UNIFONT_HEX, encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if not line or ":" not in line:
                continue
            cp_hex, data_hex = line.split(":", 1)
            table[int(cp_hex, 16)] = data_hex
    return table


# ---------------------------------------------------------------------------
# Codepoint discovery
# ---------------------------------------------------------------------------

def discover_codepoints() -> list[int]:
    """Collect every codepoint we plan to pack into glyphs.h.

    Ordering: sorted ascending so the on-device lookup can do a tight
    binary search with no extra metadata.
    """
    cps: set[int] = set()

    # (1) Printable ASCII
    for cp in range(0x20, 0x7F):
        cps.add(cp)

    # (2) Every distinct non-newline char used by sprites.json frames
    with open(SPRITES_JSON, encoding="utf-8") as f:
        data = json.load(f)
    def walk(v):
        if isinstance(v, str):
            for ch in v:
                if ch != "\n":
                    cps.add(ord(ch))
        elif isinstance(v, list):
            for x in v:
                walk(x)
        elif isinstance(v, dict):
            for x in v.values():
                walk(x)
    walk(data)

    return sorted(cps)


# ---------------------------------------------------------------------------
# Glyph extraction
# ---------------------------------------------------------------------------

def extract_bitmap(hex_data: str) -> tuple[bytes, int]:
    """Convert a Unifont .hex row into (16_bytes_8x16_bitmap, width_px).

    Unifont stores half-width glyphs as 16 hex chars (8x16 = 16 bytes) and
    full-width glyphs as 64 hex chars (16x16 = 32 bytes). We only use
    half-width here: if we hit a full-width glyph we crop the left 8 px
    so the cell width stays uniform (keeps row layout trivial).
    """
    raw = bytes.fromhex(hex_data)
    if len(raw) == 16:
        return raw, 8
    if len(raw) == 32:
        # Full-width: take left half of each row (byte 0 of each pair)
        half = bytes(raw[i] for i in range(0, 32, 2))
        return half, 8
    # Anything else is malformed — pad/truncate to 16 bytes of zeros
    return bytes(16), 8


# ---------------------------------------------------------------------------
# Emitter
# ---------------------------------------------------------------------------

HEADER_PREAMBLE = """\
// GENERATED by scripts/glyphs_to_bitmap_header.py — DO NOT EDIT by hand.
//
// Maps codepoint → 8x16 bitmap (16 bytes, 1 bit per pixel, MSB-left, row-major).
// Extracted from GNU Unifont 16.0.02 — the same source U8g2's unifont_*
// fonts are generated from, so visual output matches what the font path
// was *trying* to produce.
//
// Layout:
//   GLYPH_COUNT                 — number of entries
//   GLYPH_CODEPOINTS[GLYPH_COUNT] — sorted ascending (enables binary search)
//   GLYPH_BITMAPS[GLYPH_COUNT][16] — parallel array, one 16-byte bitmap each
//
// Regenerate with:
//   python3 scripts/glyphs_to_bitmap_header.py
#pragma once

#include <Arduino.h>

#define GLYPH_W 8
#define GLYPH_H 16
#define GLYPH_BYTES 16

"""


def emit_header(codepoints: list[int], bitmaps: list[bytes]) -> str:
    out: list[str] = [HEADER_PREAMBLE]
    out.append(f"static const uint16_t GLYPH_COUNT = {len(codepoints)};\n")
    out.append("")

    # Codepoint table
    out.append("static const uint32_t GLYPH_CODEPOINTS[] PROGMEM = {")
    line = "  "
    for i, cp in enumerate(codepoints):
        chunk = f"0x{cp:04X}"
        if i != len(codepoints) - 1:
            chunk += ","
        if len(line) + len(chunk) + 1 > 76:
            out.append(line)
            line = "  " + chunk + " "
        else:
            line += chunk + " "
    if line.strip():
        out.append(line)
    out.append("};")
    out.append("")

    # Bitmap table
    out.append("static const uint8_t GLYPH_BITMAPS[][GLYPH_BYTES] PROGMEM = {")
    import unicodedata
    for cp, bm in zip(codepoints, bitmaps):
        try:
            name = unicodedata.name(chr(cp))
        except ValueError:
            name = "(unnamed)"
        hexbytes = ", ".join(f"0x{b:02X}" for b in bm)
        out.append(f"  {{ {hexbytes} }}, // U+{cp:04X} {name}")
    out.append("};")
    out.append("")

    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    codepoints = discover_codepoints()
    print(f"[glyphs] discovered {len(codepoints)} codepoints")

    unifont = load_unifont()
    print(f"[glyphs] loaded {len(unifont)} unifont entries")

    bitmaps: list[bytes] = []
    missing: list[int] = []
    for cp in codepoints:
        if cp not in unifont:
            missing.append(cp)
            bitmaps.append(bytes(16))  # blank
            continue
        bm, _ = extract_bitmap(unifont[cp])
        bitmaps.append(bm)

    if missing:
        print(f"[glyphs] WARNING: {len(missing)} codepoints not in unifont, "
              f"emitted as blank: {[f'U+{cp:04X}' for cp in missing[:20]]}")

    header_text = emit_header(codepoints, bitmaps)
    HEADER_OUT.parent.mkdir(parents=True, exist_ok=True)
    HEADER_OUT.write_text(header_text, encoding="utf-8")

    total_bytes = len(codepoints) * 16 + len(codepoints) * 4  # bitmaps + codepoint index
    print(f"[glyphs] wrote {HEADER_OUT}")
    print(f"[glyphs] data size (PROGMEM): ~{total_bytes} bytes "
          f"({len(codepoints)} glyphs × 16 B + codepoint index)")


if __name__ == "__main__":
    main()
