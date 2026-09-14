#!/usr/bin/env python3
"""Static guard for the cna-car-simulator API boundary.

Project code (simulator/, tools/, tests/) may depend only on:

  * the XNA 4.0-compatible public API of CNA (``Microsoft/Xna/Framework/...``
    headers whose type is listed in ``scripts/xna4_types.txt``),
  * Sharp Runtime (``System/...`` headers),
  * the C++ standard library and project-owned headers (``CarSim/...``).

It must never use CNAEXT, ``EXT``-suffixed CNA extensions, CNA-internal or
renderer headers, renderer identity queries, or platform libraries (SDL, GL,
Vulkan, ...). The rules are documented in docs/api-boundary.md.

Exit code 0 means clean; 1 means violations were printed; 2 means usage error.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

SCANNED_DIRS = ("simulator", "tools", "tests")
SOURCE_SUFFIXES = {".hpp", ".cpp", ".h", ".c", ".inl", ".ipp"}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)

# Include paths that are always violations, whatever else they contain.
FORBIDDEN_INCLUDE_PATTERNS = [
    re.compile(p, re.IGNORECASE)
    for p in (
        r"(^|/)CNA/",            # CNA-specific public headers (Logger, GraphicsCapability, ...)
        r"/Internal/",           # CNA internal contract headers
        r"CnaExt|CNAEXT",        # the extended engine layer
        r"(^|/)SDL",             # SDL headers
        r"(^|/)GL/|(^|/)GLES",   # raw OpenGL / OpenGL ES
        r"vulkan",               # raw Vulkan
        r"d3d|dxgi",             # Direct3D
        r"easygl|metagl|rlgl",   # CNA renderer implementations
        r"glad|glew|epoxy",      # GL loaders
    )
]

# Identifiers that expose CNA-specific behaviour through otherwise XNA-looking
# headers. The EXT-suffix rule below catches most; these are the exceptions.
FORBIDDEN_IDENTIFIERS = [
    re.compile(r"\bCNAEXT\b"),
    re.compile(r"\bCnaExt\b"),
    re.compile(r"\bCNA\s*::"),
    re.compile(r"\b\w+EXT\b"),                 # any EXT-suffixed CNA extension
    re.compile(r"\bShaderEffect\b"),
    re.compile(r"\b(Skinned)?PbrEffect\b"),
    re.compile(r"\bColorMatrixEffect\b"),
    re.compile(r"\bAnimationPlayer\b"),
    re.compile(r"\bGetGraphicsRenderer(Name|Type)\b"),
    re.compile(r"\bSupportsCapability\b"),
    re.compile(r"\bGraphicsCapability\b"),
    re.compile(r"\bRendererCapabilityProfile\b"),
    re.compile(r"\bGetMaxTextureDimension\b"),
    re.compile(r"\bSetDepthTestEnabled\b|\bSetBlendEnabled\b|\bSetDepthWriteEnabled\b"),
    re.compile(r"\bSetCurrentEffect\b"),
    re.compile(r"\b(Easy|Meta)GL\b|\bRlgl\b|\bRLGL\b|\bVulkanRenderer\b|\bSdlRenderer\b"),
    re.compile(r"\bSDL_\w+"),
    re.compile(r"\bgl[A-Z]\w*\s*\("),           # raw GL calls
    re.compile(r"\bvk[A-Z]\w*\s*\("),           # raw Vulkan calls
]

# XNA 4.0 has no public type for these CNA header names; they are structural
# headers of the compatible API and are accepted explicitly. Every entry needs a
# reason. Keep this list short.
HEADER_ALLOWLIST = {
    # Sharp Runtime typedefs used by the XNA-compatible signatures (intcs, bytecs).
}


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string literals so documentation cannot trip rules."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j == -1 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"' or c == "'":
            quote = c
            j = i + 1
            while j < n and text[j] != quote:
                if text[j] == "\\":
                    j += 1
                if text[j] == "\n":
                    break
                j += 1
            j = min(j + 1, n)
            out.append(quote + " " * (j - i - 2) + quote if j - i >= 2 else text[i:j])
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def load_type_list(path: pathlib.Path) -> set[str]:
    types = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            types.add(line)
    return types


def header_to_type(include_path: str) -> str | None:
    """Microsoft/Xna/Framework/Graphics/BasicEffect.hpp -> Microsoft.Xna.Framework.Graphics.BasicEffect"""
    if not include_path.startswith("Microsoft/Xna/Framework/"):
        return None
    stem = include_path.rsplit(".", 1)[0]
    return stem.replace("/", ".")


def check_file(path: pathlib.Path, xna_types: set[str], violations: list[str]) -> None:
    raw = path.read_text(encoding="utf-8", errors="replace")
    code = strip_comments_and_strings(raw)
    rel = str(path)

    for match in INCLUDE_RE.finditer(raw):
        opener, inc = match.group(1), match.group(2)
        line_no = raw.count("\n", 0, match.start()) + 1
        for pat in FORBIDDEN_INCLUDE_PATTERNS:
            if pat.search(inc):
                violations.append(f"{rel}:{line_no}: forbidden include '{inc}'")
                break
        else:
            if inc.startswith("Microsoft/Xna/Framework/"):
                type_name = header_to_type(inc)
                if inc in HEADER_ALLOWLIST or type_name in xna_types:
                    continue
                violations.append(
                    f"{rel}:{line_no}: '{inc}' is not an XNA 4.0 type (not in scripts/xna4_types.txt)")
            elif inc.startswith("Microsoft/") or inc.startswith("Microsoft\\"):
                violations.append(f"{rel}:{line_no}: unexpected Microsoft header '{inc}'")
            elif opener == '"' and not (inc.startswith("CarSim/") or inc.startswith("System/")
                                        or inc.startswith("SharpRuntime/")
                                        or inc.startswith("gtest/")
                                        or ("/" not in inc and (path.parent / inc).is_file())):
                # Local includes must be project headers, Sharp Runtime, the test framework or a
                # sibling header of the including file (test helpers).
                violations.append(f"{rel}:{line_no}: unexpected local include '{inc}'")

    for line_no, line in enumerate(code.splitlines(), start=1):
        for pat in FORBIDDEN_IDENTIFIERS:
            m = pat.search(line)
            if m:
                violations.append(f"{rel}:{line_no}: forbidden identifier '{m.group(0)}'")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument("--types", default=None, help="path to xna4_types.txt")
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    types_path = pathlib.Path(args.types) if args.types else root / "scripts" / "xna4_types.txt"
    if not types_path.exists():
        print(f"error: type list not found: {types_path}", file=sys.stderr)
        return 2
    xna_types = load_type_list(types_path)

    files = []
    for d in SCANNED_DIRS:
        base = root / d
        if base.exists():
            files.extend(p for p in base.rglob("*") if p.suffix in SOURCE_SUFFIXES and p.is_file())
    files.sort()

    violations: list[str] = []
    for f in files:
        check_file(f, xna_types, violations)

    if violations:
        print(f"XNA-only API check: {len(violations)} violation(s) in {len(files)} file(s):")
        for v in violations:
            print("  " + v)
        return 1
    print(f"XNA-only API check: OK ({len(files)} files scanned, {len(xna_types)} XNA 4.0 types known)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
