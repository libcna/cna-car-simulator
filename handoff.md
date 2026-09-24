# Handoff: cna-car-simulator

Written for an AI agent (or a person) who picks this project up in a fresh context. Read this
file, then `plan.md` (the authoritative task ledger; section 26 is the current phase) and
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
tests/       GoogleTest suites, registered in tests/CMakeLists.txt (190 tests at present)
tools/       maps/build_map.py (the map pipeline), mapvalidate, font atlas generator, simtrace
scripts/     run_headless.sh, capture_set.sh, benchmark_suite.sh, benchmark_report.py,
             check_xna_only.py, check_assets.py
docs/        api-boundary, framework-findings, map-format, map-generation, vehicle-physics,
             audio-design, materials, cameras, performance, renderer-conformance,
             real-hardware-validation, research/, screenshots/ (curated set, m10-baseline/,
             renderers/)
plan.md      ledger; section 24 = Phase 11, 25 = Phase 12 (day/night, weather, signals, the
             wider region), 26 = Phase 13 (real hardware, visual realism, driving polish)
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
  every deterministic capture wants), `--weather clear|cloudy|overcast|rain|fog|snow` fixes the weather,
  `--quality low|medium|high` picks the graphics tier (default `high`, which is what every
  picture and table was taken at). `scripts/capture_set.sh` reproduces the whole curated set.
- `--route town|country|forest` drives a route from `traffic.json` with the autopilot
  (`Traffic::RouteDriver`) through the ordinary physics and exits at its end; `--route-stay`
  keeps going. `scripts/benchmark_suite.sh` runs eight deterministic scenes over a route and
  `scripts/benchmark_report.py` turns the JSON into the tables in `docs/performance.md`.
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

## State of the project (plan.md sections 24-26)

Phase 11 (realism and production quality), Phase 12 (day and night, weather, traffic signals,
four more settlements) and Phase 13 (real hardware, visual realism, driving polish) are all
complete, with their audits in sections 24.4, 25.2 and 26.6. The Phase 13 audit was run twice
from a fresh clone, because the rule adopted in section 26.2 is that **the commit advertised as
the final HEAD must itself be a commit verified from a fresh clone** -- the Phase 12 report
advertised one SHA and audited another, with an application-code change in the gap.

What Phase 13 changed, in the order it matters:

- **The map has one authoritative workflow.** `tools/maps/build_map.py` (see above and
  `docs/map-generation.md`). The stale generator that would have destroyed the square, the
  chapel, the filling station and the parked cars is gone; a ctest fails if the shipped map and
  its generator drift apart.
- **Measurement is first class.** `Traffic::RouteDriver` drives the car along the lane graph
  through the ordinary physics (`--route`), `scripts/benchmark_suite.sh` runs eight
  deterministic scenes over a route, `scripts/benchmark_report.py` prints the tables, and the
  `F3` overlay reports frames per second with the worst 1 %, the update and draw halves split by
  stage and pass, the mirror's cost and size, the weather and sun, and drawn-against-culled
  batches. `docs/real-hardware-validation.md` is the procedure for a real machine.
- **Junctions meet their roads.** A 47 cm step on a connector at Podhájí led to three fixes in
  `RoadNetwork` (concave kerb fillets, crossfall running into a flat apron, centreline height on
  a sloped plane). Over 52.4 km of lane and 148 connectors the worst deviation between the
  ground and the designed road surface is now 3.6 cm; `carsim-mapvalidate` measures it and two
  tests hold it.
- **The car reads as a car.** The sky cube map's alpha carried a four-degree sun disc that fell
  inside two texels of a 64-pixel face, so the paint never had a highlight; it now carries a
  disc, a glare lobe and a sky term at 128 pixels, and each material scales how much of it it
  takes. The A-pillar's saw-tooth edge (a world-space height test across a quad grid) follows a
  ring of the loft now, and the wing mirror is convex instead of a flat grey card.
- **Night and rain are worth driving in.** The headlamp pool reaches 40 m with a plateau and a
  soft cut-off instead of peaking at seven metres; wet roads get a grazing-angle sky sheen from a
  second additive road pass whose fog *is* the distance ramp; stars are soft points, not blue
  squares.
- **Two real signal defects**, found by a new ten-minute soak at the signalised junction: a car
  caught past the stop line crept across the box instead of clearing it, and a car that committed
  on green kept its commitment while queueing and then entered on red.
- **The mirror costs a third less** (58.9 -> 45.5 ms) from a 300 m draw-distance cap, and three
  graphics tiers (`--quality`) trade draw distance, vegetation and mirror rate.
- **The curated screenshot set is re-shot** at 19 frames (two new: `wetroad`, `rainynight`), and
  `scripts/capture_set.sh` now converts them to JPEG itself instead of printing a command with a
  scene list in it for somebody to paste -- which is how a stray `hero.png` ended up committed
  next to `hero.jpg`.
- **Renderer conformance re-measured** on all three renderers with `scripts/renderer_compare.sh`
  and `scripts/renderer_sheet.py`, and `docs/renderer-conformance.md` now says per renderer which
  of *configures / compiles / starts / renders / visually inspected / performance tested* was
  actually done. None of the visual work introduced a renderer-specific divergence.

Current scope (after the Phase 13 audit): cars, buses and lorries, overtaking, pedestrians,
walking, helicopter mode, turbo through ultra ultra turbo, rain, snow, fog, dynamic time of day,
traffic lights, wipers, tyre spray and visual body damage are all accepted functionality. The
older phase records in `plan.md` describe what existed *then*; their deferred lists do not
remove features delivered later. Phase 14 in section 27 of `plan.md` tracks the current
architecture, fidelity, traffic-rule and audio work.

Current Phase 14 progress: HUD/map/help/debug drawing and overtaking were extracted into
coherent translation units without changing their existing algorithms; the overtake planner
and the scenery construction methods in `WorldRendererScenery.cpp` now have separate ownership.
The extracted scenery methods are byte-identical to their originals and a fixed town screenshot
is byte-identical before and after; the full six-test suite passed. The overtake planner
now reads authored directional centre lines, road-wide bans and pedestrian crossings, with
weather and acceleration checks under test. A close town facade now has a framed, panelled
door and quieter plaster. The first real GPU baseline was run at `2fa9ddd` on the accelerated
AMD Radeon 780M desktop: clear-town project draw submission was 10.5 ms exterior and 16.3 ms
cockpit, of which the mirror took 5.3 ms. See `docs/performance.md` for all eight scenes and
window-throttling caveats. The restricted shell itself has no `/dev/dri`; desktop `:0` does.
The next increments polished walking acceleration, rounded pedestrian silhouettes, dense-fog
foliage culling, cockpit label spacing and night visibility, and tyre textures for gravel, snow
and wheel slip. Matched GPU screenshots are in `docs/screenshots/phase14/`; supplemental forest,
snow, fog, pedestrian, walking and aerial measurements are in `docs/performance.md`. The full
six-test suite passed after these changes. Cockpit, pedestrian, audio, forest and weather
quality still have substantial open work in `plan.md`.

Flight entry/exit and flight stepping were subsequently isolated in `VehicleFlight.cpp`; the
existing helicopter and turbo drive tests pass, and the fixed aerial frame is byte-identical
before and after. Regular car stepping still lives in `Vehicle.cpp`.

## Working conventions that kept things sane

- Every rendering change: capture before/after, compare pixel samples or half-size images,
  record what changed in the plan ledger (section 26 for Phase 13) in the same commit.
- Every defect fix: a regression test in the matching `tests/` suite, registered in
  `tests/CMakeLists.txt`; new sources registered in `simulator/CMakeLists.txt`.
- Keep temporary diagnostics (environment-variable switches, dumps) out of commits.
- Docs to keep in sync when touching an area: `docs/materials.md` (car looks),
  `docs/cameras.md`, `docs/performance.md` (numbers per scene), `docs/audio-design.md`,
  `docs/renderer-conformance.md`, `docs/real-hardware-validation.md`,
  `docs/map-generation.md` (anything under `tools/maps`).
- Label renderer, device, driver and display alongside each performance figure or image. Older
  curated images use llvmpipe; the Phase 14 baseline in `docs/performance.md` uses the Radeon.
