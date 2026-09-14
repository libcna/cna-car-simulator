#!/usr/bin/env python3
"""Rasterise a TrueType font into a bitmap-font atlas (PNG + JSON) for the simulator's
project-owned BitmapFont renderer (drawn through SpriteBatch, no SpriteFont pipeline needed).

Usage:
  python3 tools/fontatlas.py --font assets/external/fonts/d-din/D-DIN.ttf --size 32 \
      --out content/fonts/ui_regular_32 [--charset ascii|latin|plate] [--atlas 512]

Requires Pillow (python3-pil). Output:
  <out>.png   white glyphs on transparent background (tint at draw time)
  <out>.json  {"font": ..., "size": px, "lineHeight": px, "ascent": px, "atlasWidth": w,
               "atlasHeight": h, "glyphs": [{"code": int, "x","y","w","h","xoff","yoff","advance"}]}
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    print("error: Pillow is required (apt install python3-pil or pip install pillow)", file=sys.stderr)
    sys.exit(2)

CZECH = "áčďéěíňóřšťúůýžÁČĎÉĚÍŇÓŘŠŤÚŮÝŽ"
LATIN1_EXTRA = "°€×–—•‚‘’“”…"


def charset(name: str) -> str:
    ascii_set = "".join(chr(c) for c in range(32, 127))
    if name == "ascii":
        return ascii_set
    if name == "plate":
        return "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ- "
    if name == "digits":
        return "0123456789.:-/ %"
    return ascii_set + CZECH + LATIN1_EXTRA


def glyph_available(font: ImageFont.FreeTypeFont, ch: str) -> bool:
    try:
        mask = font.getmask(ch)
    except Exception:  # pragma: no cover
        return False
    if ch == " ":
        return True
    bbox = mask.getbbox()
    return bbox is not None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", required=True)
    parser.add_argument("--size", type=int, required=True, help="pixel size (em height)")
    parser.add_argument("--out", required=True, help="output base path without extension")
    parser.add_argument("--charset", default="latin", choices=["ascii", "latin", "plate", "digits"])
    parser.add_argument("--atlas", type=int, default=512, help="atlas width/height in pixels")
    parser.add_argument("--padding", type=int, default=2)
    args = parser.parse_args()

    font_path = pathlib.Path(args.font)
    font = ImageFont.truetype(str(font_path), args.size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    chars = charset(args.charset)
    missing = [c for c in chars if not glyph_available(font, c)]
    chars = [c for c in chars if c not in missing]
    if missing:
        print(f"note: {len(missing)} glyph(s) missing in {font_path.name}: {''.join(missing)}")

    atlas = Image.new("RGBA", (args.atlas, args.atlas), (255, 255, 255, 0))
    draw = ImageDraw.Draw(atlas)
    pen_x, pen_y, row_h = args.padding, args.padding, 0
    glyphs = []
    for ch in chars:
        left, top, right, bottom = font.getbbox(ch)   # relative to the pen at (0, 0) baseline-top origin
        w = max(1, right - left)
        h = max(1, bottom - top)
        advance = font.getlength(ch)
        if pen_x + w + args.padding > args.atlas:
            pen_x = args.padding
            pen_y += row_h + args.padding
            row_h = 0
        if pen_y + h + args.padding > args.atlas:
            print(f"error: atlas {args.atlas}px too small for size {args.size}", file=sys.stderr)
            return 1
        # Draw the glyph so that its bbox top-left lands on (pen_x, pen_y).
        draw.text((pen_x - left, pen_y - top), ch, font=font, fill=(255, 255, 255, 255))
        glyphs.append({
            "code": ord(ch),
            "x": pen_x, "y": pen_y, "w": w, "h": h,
            "xoff": left, "yoff": top,          # offset from the pen position (pen at the ascender line)
            "advance": round(advance, 3),
        })
        pen_x += w + args.padding
        row_h = max(row_h, h)

    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    # Store premultiplied alpha, the XNA convention that SpriteBatch's default AlphaBlend state
    # expects. The glyphs are pure white, so the premultiplied colour equals the coverage.
    _, _, _, coverage = atlas.split()
    atlas = Image.merge("RGBA", (coverage, coverage, coverage, coverage))
    atlas.save(out.with_suffix(".png"))
    meta = {
        "font": font_path.name,
        "license": "SIL Open Font License 1.1 (see assets/external/fonts/d-din/OFL-1.1.txt)",
        "size": args.size,
        "premultipliedAlpha": True,
        "charset": args.charset,
        "lineHeight": line_height,
        "ascent": ascent,
        "descent": descent,
        "atlasWidth": args.atlas,
        "atlasHeight": args.atlas,
        "glyphs": glyphs,
    }
    out.with_suffix(".json").write_text(json.dumps(meta, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {out.with_suffix('.png')} and {out.with_suffix('.json')}: {len(glyphs)} glyphs, line height {line_height}px")
    return 0


if __name__ == "__main__":
    sys.exit(main())
