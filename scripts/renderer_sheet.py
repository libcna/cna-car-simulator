#!/usr/bin/env python3
"""Builds the comparison sheet for one scene from the images scripts/renderer_compare.sh wrote.

    python3 scripts/renderer_sheet.py build/renderers town docs/screenshots/renderers/town.png

The sheet is a 2 x 2 grid: OPENGLES3, OPENGL33, SOFTWARE, and the SOFTWARE-minus-OPENGLES3
difference amplified four times so a one-level shift is visible. It also prints the mean and tail
of each pairwise difference, which is what the table in docs/renderer-conformance.md holds.

Needs Pillow. Reference is always OPENGLES3, the renderer everything else in the project uses.
"""
import pathlib
import sys

from PIL import Image, ImageChops, ImageDraw


def stats(a, b):
    d = ImageChops.difference(a, b)
    px = d.load()
    w, h = d.size
    total = over32 = over96 = 0
    for y in range(h):
        for x in range(w):
            m = max(px[x, y])
            total += m
            over32 += m > 32
            over96 += m > 96
    n = w * h
    return total / n, over32 / n * 100.0, over96 / n * 100.0


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    source, scene, out = pathlib.Path(argv[1]), argv[2], pathlib.Path(argv[3])
    names = ["opengles3", "opengl33", "software"]
    images = {}
    for name in names:
        path = source / f"{name}-{scene}.png"
        if not path.is_file():
            print(f"missing {path}", file=sys.stderr)
            return 1
        images[name] = Image.open(path).convert("RGB")

    reference = images["opengles3"]
    for name in names[1:]:
        mean, over32, over96 = stats(images[name], reference)
        print(f"{name} vs opengles3, {scene}: mean {mean:.2f}/255, >32 {over32:.2f} %, >96 {over96:.3f} %")

    amplified = ImageChops.difference(images["software"], reference).point(lambda v: min(255, v * 4))
    cell = (reference.width // 2, reference.height // 2)
    sheet = Image.new("RGB", (cell[0] * 2, cell[1] * 2))
    labels = [("opengles3", (0, 0)), ("opengl33", (cell[0], 0)), ("software", (0, cell[1]))]
    for name, at in labels:
        sheet.paste(images[name].resize(cell, Image.LANCZOS), at)
    sheet.paste(amplified.resize(cell, Image.LANCZOS), (cell[0], cell[1]))
    draw = ImageDraw.Draw(sheet)
    captions = [("OPENGLES3", (6, 4)), ("OPENGL33", (cell[0] + 6, 4)),
                ("SOFTWARE", (6, cell[1] + 4)), ("SOFTWARE - OPENGLES3, x4", (cell[0] + 6, cell[1] + 4))]
    for text, at in captions:
        draw.text((at[0] + 1, at[1] + 1), text, fill=(0, 0, 0))
        draw.text(at, text, fill=(255, 255, 0))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"wrote {out} ({sheet.width} x {sheet.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
