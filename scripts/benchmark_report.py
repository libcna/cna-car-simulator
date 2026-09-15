#!/usr/bin/env python3
"""Turns the JSON written by scripts/benchmark_suite.sh into a Markdown table.

    python3 scripts/benchmark_report.py build/benchmarks --label "Debian 13 / Radeon 780M"
    python3 scripts/benchmark_report.py build/benchmarks --against docs/benchmarks/before

The table is what goes into docs/performance.md. With --against, a second directory of the same
scenes is read and the difference is shown, which is how a before/after comparison is reported:
percentages, not adjectives.

Every row says which environment produced it, because a frame time means nothing without one.
"""
import argparse
import json
import pathlib
import sys

SCENES = ["day_chase", "day_cockpit", "rain_chase", "rain_cockpit",
          "night_chase", "night_cockpit", "rainynight_chase", "rainynight_cockpit"]
TITLES = {
    "day_chase": "clear day, exterior", "day_cockpit": "clear day, cockpit",
    "rain_chase": "rain, exterior", "rain_cockpit": "rain, cockpit",
    "night_chase": "clear night, exterior", "night_cockpit": "clear night, cockpit",
    "rainynight_chase": "rainy night, exterior", "rainynight_cockpit": "rainy night, cockpit",
}


def load(directory):
    out = {}
    for scene in SCENES:
        path = pathlib.Path(directory) / f"{scene}.json"
        if path.is_file():
            out[scene] = json.loads(path.read_text(encoding="utf-8"))
    return out


def fps(entry):
    ms = entry.get("frameMsAvg", 0.0)
    return 1000.0 / ms if ms > 0.0001 else 0.0


def delta(now, before):
    if before is None or before <= 0.0001:
        return ""
    change = (now - before) / before * 100.0
    return f" ({change:+.1f} %)"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", help="directory of scene JSON files")
    ap.add_argument("--label", default="", help="what machine and renderer produced these numbers")
    ap.add_argument("--against", help="a second directory to compare against")
    args = ap.parse_args(argv)

    now = load(args.directory)
    if not now:
        print(f"no scene files in {args.directory}", file=sys.stderr)
        return 1
    before = load(args.against) if args.against else {}

    any_entry = next(iter(now.values()))
    header = args.label or "unlabelled run"
    print(f"### {header}")
    print()
    print(f"{any_entry.get('width', '?')} x {any_entry.get('height', '?')}, "
          f"{any_entry.get('frames', '?')} frames per scene after {any_entry.get('warmupFrames', '?')} warm-up frames, "
          "one simulation step per drawn frame.")
    print()
    print("| scene | fps | frame ms | worst 1 % | update ms | draw ms | draw calls | triangles |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for scene in SCENES:
        if scene not in now:
            continue
        e = now[scene]
        b = before.get(scene)
        print(f"| {TITLES[scene]} | {fps(e):.1f}{delta(fps(e), fps(b) if b else None)} "
              f"| {e['frameMsAvg']:.1f}{delta(e['frameMsAvg'], b['frameMsAvg'] if b else None)} "
              f"| {e.get('frameMsWorst1pc', 0.0):.1f} "
              f"| {e['updateMsAvg']:.2f} | {e['drawMsAvg']:.1f} "
              f"| {e['drawCallsAvg']:.0f} | {e['trianglesAvg'] / 1e6:.2f} M |")
    print()
    print("Where the draw time goes (average ms per frame):")
    print()
    print("| scene | sky | world | traffic | car | mirror | cluster | hud |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for scene in SCENES:
        if scene not in now:
            continue
        p = now[scene]["passesMsAvg"]
        print(f"| {TITLES[scene]} | {p['sky']:.1f} | {p['world']:.1f} | {p['traffic']:.1f} "
              f"| {p['vehicle']:.1f} | {p['mirror']:.1f} | {p['cluster']:.2f} | {p['hud']:.2f} |")
    print()
    print("Where the update time goes (average ms per frame):")
    print()
    print("| scene | vehicle | collision | traffic AI | audio |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for scene in SCENES:
        if scene not in now:
            continue
        u = now[scene].get("updateMsAvgSplit")
        if not u:
            continue
        print(f"| {TITLES[scene]} | {u['vehicle']:.3f} | {u['collision']:.3f} | {u['traffic']:.3f} | {u['audio']:.3f} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
