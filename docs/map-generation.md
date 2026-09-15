# Map generation: one authoritative workflow

Status: decided 2026-09-15 (plan task `RH-002`). Supersedes the ad-hoc mixture of a stale
generator script plus hand edits that preceded it.

## The pipeline

```
source definition            tools/maps/generate_lipova.py  (stage 1)
                             tools/maps/add_settlements.py  (stage 2)
        |
        v
generation pipeline          tools/maps/build_map.py
        |
        v
generated map                content/maps/lipova/{map,terrain,roads,objects,traffic}.json
        |
        v
map validator                carsim-mapvalidate  (ctest: map_validate_lipova)
        |
        v
runtime                      the simulator loads the generated JSON
```

The JSON under `content/maps/lipova` is **generated output that is committed**, in the same way
a lockfile is. It is committed so the simulator builds and runs without Python, and so map
changes are reviewable as diffs; it is generated so that there is exactly one place to change
the map, and no way for the source and the shipped content to drift apart unnoticed.

## Commands

```bash
python3 tools/maps/build_map.py                # rebuild content/maps/lipova in place
python3 tools/maps/build_map.py --validate     # ... and run carsim-mapvalidate over the result
python3 tools/maps/build_map.py --out DIR      # build somewhere else, leaving the shipped map alone
python3 tools/maps/build_map.py --check        # build into a temporary directory and diff
```

`--check` is registered as the ctest test **`map_regeneration_check`**. It fails when the
committed map is not what the pipeline produces, and prints the offending diff. The workflow for
any map change is therefore:

1. edit the stage that owns the content (see the ownership table below),
2. `python3 tools/maps/build_map.py --validate`,
3. commit the stage change **and** the regenerated JSON together.

Committing one without the other fails `map_regeneration_check` in CI.

## The stages

Stages run in a fixed order and each is deterministic: the same source always produces the same
bytes. There is no randomness that is not seeded, and no dependence on dictionary iteration
order, the clock, or the file system.

| Stage | Module | Writes | Idempotent because |
| --- | --- | --- | --- |
| 1 | `generate_lipova.py` | all five files, from scratch | it replaces the whole map |
| 2 | `add_settlements.py` | four of the five, in place | it deletes its own output first |

**Stage 1 — `generate_lipova.py`** writes the complete base map: the nodes and roads of Lipová
and its countryside, the terrain, the paved town square with its frontages, limes, benches and
memorial, the wayside chapel at the signalised junction, the filling station and its forecourt,
the parked cars, every sign and prop, and the four original player spawns plus `kostel`. It
truncates whatever was in the output directory, so it must be run first.

**Stage 2 — `add_settlements.py`** grows that into the 6.4 x 7.6 km region: Březí, Podhájí, Nové
Město and Kamenice, the three class III roads that reach them, the larger terrain and the hills
under them, and a spawn in each. It is *additive*: it reads what stage 1 left, removes only the
entries it wrote on an earlier run — every one of them carries a `"generated-by":
"add_settlements"` key, or a node/road id from its own list — and appends the current ones. It
also rewrites the map card's description, measuring the road length from the roads it has just
written rather than repeating a number from a comment. Running it twice in a row is the same as
running it once; there is a test for that.

Neither stage may be run by accident: both refuse without `--stage-only`, and point at
`build_map.py`. This is deliberate. Before this pipeline existed, `generate_lipova.py` was
several passes of content out of date and re-running it silently destroyed the square, the
chapel, the filling station and the parked cars. Nothing in this repository should ever again be
a script you are warned not to run.

## Who owns what

| Content | Stage |
| --- | --- |
| Lipová: nodes, roads, signals at E1 | 1 |
| Terrain size, cell size, base features and regions of the inner map | 1 (stage 2 enlarges size/cell and prepends the outer regions) |
| Town square: paving, frontages, limes, benches, bins, lamps, memorial | 1 |
| Wayside chapel, filling station, forecourt paving, pumps, canopy | 1 |
| Parked cars (`objects.vehicles`) | 1 |
| Signs and props along the original roads | 1 |
| Březí, Podhájí, Nové Město, Kamenice and their roads, signs, props, avenues | 2 |
| Outer terrain regions and hills | 2 |
| Map card description and measured road length | 2 |

Region order matters at runtime (a later region wins where two overlap), which is why stage 2
puts its outer regions *in front of* the inner ones: the town, the square and the station
forecourt must stay on top.

## Determinism notes

- Everything random is drawn from an explicitly seeded `random.Random`; no global `random` calls.
- Positions are rounded to 0.1 m and headings to 0.1 deg as they are written, so small changes to
  floating-point evaluation order cannot ripple into the output.
- Building seeds are assigned from fixed bases (7100, 7200, 7300 round the square; 8100, 8200,
  8300 for the parked bays) so adding content in one place does not renumber another.
- JSON is written with `indent=1`, `ensure_ascii=False` and a trailing newline, which is what
  makes the byte-for-byte `--check` comparison possible.

## Adding a settlement or a feature later

Add it to stage 2 if it is new content in the wider region; add it to stage 1 if it belongs to
Lipová itself. If a third stage is ever wanted, register it in `STAGES` in `build_map.py` and
give it the same contract: deterministic, idempotent, and marking its own output so a re-run can
find it.
