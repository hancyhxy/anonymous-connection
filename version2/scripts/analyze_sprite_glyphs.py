#!/usr/bin/env python3
"""
Inspect glyph usage in `web/sprites.json`.

Usage:
  python3 scripts/analyze_sprite_glyphs.py
  python3 scripts/analyze_sprite_glyphs.py male_1_0 female_4_0

Outputs:
  - full non-ASCII glyph inventory across all sprites
  - per-key glyph inventory for any requested sprite keys

This is useful for debugging which Unicode BMP symbols are used by a given
avatar page before deciding whether to replace or rasterize them for device
display.
"""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPRITES_PATH = ROOT / "web" / "sprites.json"


def load_sprites() -> dict[str, list[str]]:
    return json.loads(SPRITES_PATH.read_text(encoding="utf-8"))


def iter_chars(frames: list[str]):
    for frame in frames:
        for ch in frame:
            if ch not in {" ", "\n", "\r", "\t"}:
                yield ch


def format_chars(chars):
    return " ".join(f"U+{ord(ch):04X}:{ch}" for ch in sorted(chars, key=ord))


def main(argv: list[str]) -> int:
    sprites = load_sprites()

    wanted = argv[1:]
    glyph_counter: Counter[str] = Counter()
    for frames in sprites.values():
        glyph_counter.update(ch for ch in iter_chars(frames) if ord(ch) > 127)

    print("== Full non-ASCII glyph inventory ==")
    for ch, count in sorted(glyph_counter.items(), key=lambda item: ord(item[0])):
        print(f"U+{ord(ch):04X}\t{ch}\t{count}")

    if wanted:
        print()
        print("== Per-key glyph inventory ==")

    for key in wanted:
        frames = sprites.get(key)
        if frames is None:
            print(f"{key}: NOT FOUND")
            continue

        used = set(iter_chars(frames))
        non_ascii = {ch for ch in used if ord(ch) > 127}
        ascii_only = sorted(ch for ch in used if ord(ch) < 128)

        print(f"{key}")
        if non_ascii:
            print(f"  non_ascii: {format_chars(non_ascii)}")
        else:
            print("  non_ascii: <none>")
        if ascii_only:
            print(f"  ascii: {' '.join(ascii_only)}")
        else:
            print("  ascii: <none>")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
