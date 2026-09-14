#!/usr/bin/env python3
"""Asset provenance audit.

Every file under assets/external/ must be listed in assets/manifest.json with a licence and a
SHA-256 that matches the file on disk, and every manifest entry must point at existing files.
Runs as the `asset_manifest_check` CTest and can be run by hand:

    python3 scripts/check_assets.py --root .
"""
import argparse
import hashlib
import json
import pathlib
import sys

ALLOWED_LICENSES = {"CC0-1.0", "MIT", "OFL-1.1", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "CC-BY-4.0", "Zlib"}


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    manifest_path = root / "assets" / "manifest.json"
    external = root / "assets" / "external"
    problems: list[str] = []
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as ex:
        print(f"asset check: cannot read {manifest_path}: {ex}")
        return 1
    listed: set[pathlib.Path] = set()
    for asset in manifest.get("assets", []):
        aid = asset.get("id", "?")
        for key in ("sourceUrl", "license", "files", "author", "retrieved"):
            if key not in asset:
                problems.append(f"{aid}: missing '{key}'")
        if asset.get("attributionRequired") and not asset.get("attributionText"):
            problems.append(f"{aid}: attribution required but no attributionText")
        lic = asset.get("license", "")
        if lic not in ALLOWED_LICENSES:
            problems.append(f"{aid}: licence '{lic}' is not in the allowed set {sorted(ALLOWED_LICENSES)}")
        for entry in asset.get("files", []):
            rel = entry.get("path")
            digest = entry.get("sha256", "")
            if not rel:
                problems.append(f"{aid}: file entry without path")
                continue
            path = root / rel
            listed.add(path.resolve())
            if not path.is_file():
                problems.append(f"{aid}: missing file {rel}")
                continue
            actual = sha256(path)
            if actual != digest:
                problems.append(f"{aid}: sha256 mismatch for {rel}: manifest {digest[:12]}..., file {actual[:12]}...")
    if external.is_dir():
        for path in sorted(p for p in external.rglob("*") if p.is_file()):
            if path.resolve() not in listed:
                problems.append(f"unlisted external asset: {path.relative_to(root)}")
    if problems:
        print("asset check: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print(f"asset check: OK ({len(listed)} files listed, {len(manifest.get('assets', []))} assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
