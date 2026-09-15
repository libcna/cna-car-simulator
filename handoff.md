# Handoff: cna-car-simulator

Written for an AI agent (or a person) who picks this project up in a fresh context. Read this
file, then `plan.md` (the authoritative task ledger; section 24 is the current phase) and
`README.md`. Everything below is verified against the repository state at the time of writing;
re-verify with `git log` and the ledger before acting.

## What this project is

A small, realistic passenger-car driving simulator in a fictional Czech landscape, C++23,
built on the **XNA 4.0-compatible public API** of the CNA framework (branch `next`) and Sharp
Runtime (branch `next`). One car (Lipan 1.2, procedural), one map (a 6.4 x 7.6 km region around
Lipová with four more settlements), ambient traffic with working signals, a day and night cycle,
weather, cockpit with live cluster and mirror, procedural audio. No missions, no economy.

Hard constraints (from the project owner; do not relax):

- Only CNA's XNA 4.0 public API, project-owned code and standard C++. No CNAEXT, no CNA
  renderer internals, no renderer-specific APIs (EasyGL, RLGL, DirectX, SDL graphics internals,
  Vulkan/OpenGL bypasses). `scripts/check_xna_only.py` enforces this and must stay in place.
- Never remove or weaken tests. Refactor only for concrete defects, with regression tests.
- Legal assets only (everything is generated in code; `scripts/check_assets.py`).
- Git: work on branch `claude/cna-car-simulator-project-scx0ij`, commit in logical units, push
  regularly with `git push -u origin <branch>`, never force-push or rewrite history, never open
  a pull request unless asked. Commit messages end with the attribution trailers the session
  is given (a `Co-Authored-By:` line and a `Claude-Session:` line); no model identifiers in code
  or docs.
- Work autonomously; do not stop to ask when a careful colleague would just decide.

## Repository layout (short)

```
simulator/include/CarSim/<Area>/   headers      simulator/src/<Area>/   sources
  Core (command line, save data), Sim (vehicle physics), Map, Collision, Traffic, Audio,
  Input, Render (world/road/building/vegetation/sign/car/cockpit generators, cameras,
  cluster, mirror, sky, shadows), App (SimulatorGame, Program)
content/     vehicles/lipan_12.json, maps/lipova/{map,roads,terrain,objects,traffic}.json, fonts/
tests/       GoogleTest suites, registered in tests/CMakeLists.txt (178 tests at present)
tools/       map generator/validator, font atlas generator, simulation tracer
scripts/     run_headless.sh, check_xna_only.py, check_assets.py
docs/        api-boundary, framework-findings, map-format, vehicle-physics, audio-design,
             materials, cameras, performance, renderer-conformance, real-hardware-validation,
             research/, screenshots/ (curated set, m10-baseline/, renderers/)
plan.md      ledger; section 24 = Phase 11, section 25 = Phase 12 (day/night, weather,
             signals, the wider region)
```

## Building and testing in this environment

Dependencies are NOT sibling checkouts here; they live under `/home/user/deps`:

```bash
cmake --preset opengles3 -DCARSIM_CNA_ROOT=/home/user/deps/cna -DCARSIM_SHARP_RUNTIME_ROOT=/home/user/deps/sharp-runtime
cmake --build build/opengles3 -j4            # targets: cna-car-simulator, carsim_tests, tools
./build/opengles3/bin/carsim_tests           # unit + scenario tests (~20 s)
ctest --preset opengles3                     # also the headless smoke tests (needs Xvfb)
/usr/bin/python3.12 scripts/check_xna_only.py && /usr/bin/python3.12 scripts/check_assets.py
```

Other presets (`opengl33`, `software`) configure the same way; both were built and run
(`docs/renderer-conformance.md`). `vulkan` has no ICD in the container. A full CNA build takes
about 15 minutes with `-j2`. Use `/usr/bin/python3.12` for anything needing Pillow (the other
Pythons lack it).

The simulator binary reads `build/opengles3/bin/content` (copied at build time) and falls back
to the source `content/`; after editing content JSON either rebuild or pass `--content content`.

## Headless captures (the screenshot loop)

```bash
scripts/run_headless.sh ./build/opengles3/bin/cna-car-simulator --no-save --no-audio --lockstep \
    --spawn square --frames 480 --auto-drive 8 --traffic-warmup 40 --screenshot out.png
```

- `--lockstep`: one 1/60 s simulation step per drawn frame, so `--frames N` = N/60 s of
  simulated time (480 frames = 8 s; the car reaches about 35 km/h with `--auto-drive 8`).
- Content edits alone do not reach the binary: the content directory is copied next to the
  executable by a POST_BUILD step of the simulator target, so after editing JSON either touch a
  source file and rebuild, or pass `--content content`.
- Spawns in `content/maps/lipova/traffic.json`: `square` (east-bound in town), `forest`
  (forest edge, heading NNW), `fields` (avenue through the fields), `east` (main road east of
  town), `kostel` (90 m west of the signalised junction E1, east-bound), and one in each of the
  new settlements: `brezi`, `podhaji`, `mesto`, `kamenice`.
- `--cockpit` (+ `--screenshot-cluster file`), `--chase-yaw <deg>` (positive = orbit to the
  car's right), `--chase-distance <m>` (INTEGER, `5.5` is rejected), `--view x y z heading pitch`
  (fixed camera; y is absolute, terrain is not flat, check for underground views),
  `--eye dx dy dz yaw pitch` (cockpit eye offset), `--lights`, `--benchmark`,
  `--benchmark-json file`, `--mirror-every n`, `--debug-overlay`.
- `--time <hh:mm>` and `--time-scale <x>` fix the clock (`--time-scale 0` freezes the sky, which
  every deterministic capture wants), `--weather clear|cloudy|overcast|rain` fixes the weather.
  `scripts/capture_set.sh` reproduces the whole curated set in one command.
- llvmpipe renders 0.2-0.5 s per frame in town; long captures run in the background.
  Downscale to half size before viewing to save context.

## Renderer facts learned the hard way (see docs/framework-findings.md too)

- Stencil clears are unreliable on the EasyGL path after the second frame: no shadow or effect
  relies on stencil (vehicle shadow is a draped convex-hull blob; building/tree shadows are
  baked into the terrain macro texture and road vertex colours).
- `DrawUserPrimitives` drew nothing after the world pass in town; dynamic geometry uses
  VertexBuffer/IndexBuffer with per-frame `SetData`.
- CNA's `DualTextureEffect` does not double detail x macro (unlike XNA); the terrain macro
  carries the full light.
- The OPENGLES3 path on llvmpipe shows anisotropic streaks on the asphalt at grazing angles;
  OPENGL33 and SOFTWARE do not. Not project code.
- Triangles are emitted clockwise-front; outward normal = -cross(b-a, c-a);
  `MeshData::SignedVolume()` is negative when outward, `OrientOutward()` fixes inside-out lofts.
- Chase camera convention: yaw = atan2(-fwd.x, -fwd.z), behind = (sin, 0, cos), right =
  (cos, 0, -sin). It was mirrored until commit dc48f78 (camera in front of east-bound cars).
- Details on the car body must be projected onto the skin (`SkinGrid::Sample`, and
  `FrontFacePoint` for the nose): the loft sweeps inwards at the nose and tail, so a position
  computed from the centre-line setback floats beside the bumper at the corners.
- Lamp lenses are decals cut from the skin in UV space; a skin quad is turned into a recessed
  housing only when all four of its corners are inside the lens polygon, otherwise the housing
  shows as black notches around the lens.

## Day and night, weather, signals (plan.md section 25)

- `LightingRig` has a clock (`SetTimeOfDay`) and weather (`SetWeather`). Everything baked -- the
  terrain macro, the ground shadows, the road and verge vertex colours -- stays baked under
  `LightingRig::BakeReference()` (10:30, clear) and is scaled per frame by `BakedLightingScale`.
  `WorldRenderer` holds that reference as `bakeRig_`; baking under the live rig darkens the
  ground twice and is the first thing to check if the ground looks wrong at an odd hour.
- Cloud is modelled as a *redistribution*: the key light collapses and most of what it loses
  comes back as flat ambient and sky fill. Mixing towards an absolute grey instead lights an
  overcast midnight like an overcast noon -- there is a test for that.
- `LightingRig::LampFactor()` drives the lit-window batches, the street lamp pools and the
  headlamp pool. The clock and the weather are read before the renderers exist, so `LoadContent`
  ends with an explicit `RefreshLighting(true)`.
- Signals: `Map::SignalPlan` on a node, `Traffic::AspectAt` is a pure function of the plan, the
  group and the elapsed time (so warmed-up scenarios are reproducible), `SignalRenderer` draws
  the lenses. The masts are ordinary props placed by `ObjectPlacement::PlaceTrafficSignals`.

## The map and its authoring scripts

One entry point: `python3 tools/maps/build_map.py` (add `--validate` to run the map validator
over the result). It runs stage 1 `generate_lipova.py` (the whole town, square, chapel, filling
station, parked cars -- written from scratch) then stage 2 `add_settlements.py` (the wider
region -- additive and idempotent). The JSON under `content/maps/lipova` is generated output
that is committed; change a stage and commit the regenerated map with it, or the ctest
`map_regeneration_check` (`build_map.py --check`) fails. Neither stage runs without
`--stage-only`. Full description in `docs/map-generation.md`.

## State of the project (plan.md section 24)

Phase 11 ("Realism & Production Quality") is complete and its final audit is recorded in
section 24.4; section 24.5 holds the follow-up work done after that audit, all of it `[x]`:

- `RQ-160` the paved town square (a `square` terrain region drawn with generated granite setts,
  cobbles under the wheels, town houses lining three sides, lime trees, benches, lamps),
- `RQ-161` parked cars (`objects.vehicles[]`, drawn by `TrafficRenderer::DrawParked`, solid in
  the collision world),
- `RQ-162` the memorial column on the square,
- `RQ-163` street parking generated along urban local and residential streets.

Section 24.5 now also covers a content pass on the sample map: prefab block facades
(`RQ-164`), the forest wrapped around the track loop (`RQ-165`), a wayside chapel at the
eastern junction (`RQ-166`), planted gardens behind the houses (`RQ-167`), a horizon apron so
the terrain no longer ends in mid-air (`RQ-168`), help-overlay wording (`RQ-169`), a filling
station with `yard` paving and exact four-corner paved outlines (`RQ-171`) and meadow trees
(`RQ-172`). Audits of the follow-up work are recorded as `RQ-170` and `RQ-173`.

Open / next ideas (nothing is blocking):

- Traffic variety: there is no bus or lorry body class, and no overtaking or lane changing.
- The fog lamp lens is dark gloss rather than a clear lens; shop fascia signs are missing.
- Baked shadow *direction* does not follow the sun, only its strength (a known limitation of
  scaling the bake instead of re-baking).
- Snow and fog are not modelled; the weather is clear / cloud / overcast / rain.
- Nové Město has a square laid out in buildings but no paved `square` region of its own.

## Working conventions that kept things sane

- Every rendering change: capture before/after, compare pixel samples or half-size images,
  record what changed in the plan ledger (section 25 for Phase 12) in the same commit.
- Every defect fix: a regression test in the matching `tests/` suite, registered in
  `tests/CMakeLists.txt`; new sources registered in `simulator/CMakeLists.txt`.
- Keep temporary diagnostics (environment-variable switches, dumps) out of commits.
- Docs to keep in sync when touching an area: `docs/materials.md` (car looks),
  `docs/cameras.md`, `docs/performance.md` (numbers per scene), `docs/audio-design.md`,
  `docs/renderer-conformance.md`, `docs/real-hardware-validation.md`.
