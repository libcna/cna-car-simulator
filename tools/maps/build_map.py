#!/usr/bin/env python3
"""The one entry point that builds the sample map "Lipová" from its source definition.

    source definition (the stage modules in this directory)
        -> deterministic generation pipeline (this script)
        -> the map under content/maps/lipova
        -> the map validator (carsim-mapvalidate)
        -> the simulator

Stages run in order and each one is deterministic; together they always produce the same bytes
from the same source, so the shipped map can be rebuilt at any time and the result checked
against what is in the repository.

    stage 1  generate_lipova   writes the whole map from scratch: Lipová, its roads, terrain,
                               square, chapel, filling station, signs, props, trees and spawns
    stage 2  add_settlements   grows that into the wider region (Březí, Podhájí, Nové Město,
                               Kamenice). Additive and idempotent: it replaces only its own
                               output, marked with a "generated-by" key
    stage 3  add_castle        adds Hrad Lipník on its wooded hill west of Lipová and the forest
                               track up to it. Additive and idempotent like stage 2

Usage:

    python3 tools/maps/build_map.py                 rebuild content/maps/lipova in place
    python3 tools/maps/build_map.py --out DIR       build into DIR instead
    python3 tools/maps/build_map.py --check         build into a temporary directory and report
                                                    any difference from the shipped map
    python3 tools/maps/build_map.py --validate      also run carsim-mapvalidate over the result

--check is what the map_regeneration_check test runs: it fails if the shipped map and the
pipeline have drifted apart, which is the failure mode this pipeline exists to prevent.
"""
import argparse
import difflib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SHIPPED = ROOT / "content" / "maps" / "lipova"
FILES = ["map.json", "terrain.json", "roads.json", "objects.json", "traffic.json"]

sys.path.insert(0, str(HERE))
import add_castle                                                 # noqa: E402
import add_settlements                                            # noqa: E402
import generate_lipova                                            # noqa: E402

STAGES = [("generate_lipova", generate_lipova.build), ("add_settlements", add_settlements.build), ("add_castle", add_castle.build)]


def build(out_dir, log=print):
    """Run every stage in order into out_dir."""
    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for name, stage in STAGES:
        if log:
            log(f"stage {name}")
        stage(out, log=log)
    return out


def find_validator():
    for preset in ("opengles3", "opengl33", "software"):
        candidate = ROOT / "build" / preset / "bin" / "carsim-mapvalidate"
        if candidate.is_file():
            return candidate
    found = shutil.which("carsim-mapvalidate")
    return pathlib.Path(found) if found else None


def validate(map_dir, log=print):
    """Run the map validator over map_dir. Returns True when it passes, None when it is absent."""
    validator = find_validator()
    if validator is None:
        if log:
            log("map validator not built; skipping validation "
                "(build the carsim-mapvalidate target to include it)")
        return None
    result = subprocess.run([str(validator), str(map_dir)], capture_output=True, text=True)
    tail = [line for line in result.stdout.splitlines() if line.startswith(("objects:", "placed:", "map-validate:"))]
    if log:
        for line in tail:
            log(f"  {line}")
        if result.returncode != 0:
            log(result.stdout[-4000:])
            log(result.stderr[-2000:])
    return result.returncode == 0


def differences(built, shipped=SHIPPED):
    """Unified diffs of every map file that the pipeline no longer reproduces."""
    out = []
    for name in FILES:
        a = (pathlib.Path(shipped) / name)
        b = (pathlib.Path(built) / name)
        left = a.read_text(encoding="utf-8").splitlines(keepends=True) if a.is_file() else []
        right = b.read_text(encoding="utf-8").splitlines(keepends=True) if b.is_file() else []
        if left == right:
            continue
        diff = list(difflib.unified_diff(left, right, f"shipped/{name}", f"rebuilt/{name}", n=1))
        out.append((name, diff))
    return out


def counts(map_dir):
    """A short census of the built map, for the log."""
    objects = json.loads((pathlib.Path(map_dir) / "objects.json").read_text(encoding="utf-8"))
    roads = json.loads((pathlib.Path(map_dir) / "roads.json").read_text(encoding="utf-8"))
    return (f"{len(roads['nodes'])} nodes, {len(roads['roads'])} roads, "
            f"{len(objects['buildings'])} buildings, {len(objects['signs'])} signs, "
            f"{len(objects['props'])} props, {len(objects.get('vehicles', []))} parked cars")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="build into this directory instead of the shipped map")
    ap.add_argument("--check", action="store_true",
                    help="build into a temporary directory and fail if it differs from the shipped map")
    ap.add_argument("--validate", action="store_true", help="run carsim-mapvalidate over the result")
    ap.add_argument("--quiet", action="store_true", help="only report problems")
    args = ap.parse_args(argv)
    log = None if args.quiet else print

    if args.check:
        with tempfile.TemporaryDirectory(prefix="lipova-rebuild-") as tmp:
            build(tmp, log=log)
            # The additive stages must be idempotent: running any of them again, followed by the
            # stages after it, has to give the same map, or a second run of the pipeline would
            # keep growing it. Checked from the last stage back, so the first failure names the
            # stage at fault.
            snapshot = {name: (pathlib.Path(tmp) / name).read_bytes() for name in FILES}
            for first in range(len(STAGES) - 1, 0, -1):
                for _, stage in STAGES[first:]:
                    stage(tmp, log=None)
                repeated = [f for f in FILES if (pathlib.Path(tmp) / f).read_bytes() != snapshot[f]]
                if repeated:
                    print(f"map-regeneration: FAILED ({STAGES[first][0]} is not idempotent; it changed "
                          f"{', '.join(repeated)} on a second run)")
                    return 1
            drift = differences(tmp)
            if args.validate and validate(tmp, log=log) is False:
                print("map-regeneration: FAILED (the rebuilt map does not validate)")
                return 1
            if drift:
                print(f"map-regeneration: FAILED ({len(drift)} file(s) differ from content/maps/lipova)")
                for name, diff in drift:
                    print(f"--- {name}: {sum(1 for l in diff if l.startswith(('+', '-')) and not l.startswith(('+++', '---')))} changed line(s)")
                    sys.stdout.writelines(diff[:120])
                print("\nThe shipped map and its generator have drifted apart. Either rebuild the map\n"
                      "(python3 tools/maps/build_map.py) or bring the stage that owns this content up to date.")
                return 1
            print(f"map-regeneration: OK (the pipeline reproduces content/maps/lipova exactly; {counts(tmp)})")
        return 0

    target = pathlib.Path(args.out) if args.out else SHIPPED
    build(target, log=log)
    if log:
        log(f"built {target}: {counts(target)}")
    if args.validate and validate(target, log=log) is False:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
