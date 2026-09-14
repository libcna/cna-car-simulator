#!/usr/bin/env python3
"""Regenerate scripts/xna4_types.txt from a checkout of https://github.com/libcna/xna4-spec.

Usage: python3 scripts/generate_xna4_type_list.py /path/to/xna4-spec
"""
import pathlib
import sys
import xml.etree.ElementTree as ET


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    spec = pathlib.Path(sys.argv[1]) / "index.xml"
    root = ET.parse(spec).getroot()
    names = sorted(
        f"{ns.get('name')}.{t.get('name')}"
        for ns in root.findall("namespace")
        for t in ns.findall("type-ref")
    )
    out = pathlib.Path(__file__).resolve().parent / "xna4_types.txt"
    with out.open("w", encoding="utf-8") as f:
        f.write("# XNA 4.0 public types, generated from https://github.com/libcna/xna4-spec (index.xml, MIT).\n")
        f.write("# Regenerate with: python3 scripts/generate_xna4_type_list.py <path-to-xna4-spec>\n")
        f.write("# One fully qualified type name per line. Used by scripts/check_xna_only.py.\n")
        f.write("\n".join(names) + "\n")
    print(f"{len(names)} types written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
