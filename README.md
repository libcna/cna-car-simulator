# cna-car-simulator

A realistic passenger-car driving simulator set in a fictional Czech landscape, written in
C++23 on the **XNA 4.0-compatible public API** of the [CNA](https://github.com/libcna/cna)
framework (branch `next`) and [Sharp Runtime](https://github.com/libcna/sharp-runtime)
(branch `next`).

There are no jobs, missions, deliveries or economy. You start a car, drive through a Czech
town and the villages around it, the countryside and a forest, meet traffic, and enjoy driving
-- at any hour and in any weather.

| Hero | Cockpit | Instrument cluster |
| --- | --- | --- |
| ![Lipan 1.2 on the square](docs/screenshots/hero.jpg) | ![Cockpit at 35 km/h](docs/screenshots/cockpit.jpg) | ![Instrument cluster](docs/screenshots/cluster.png) |

| Town | Traffic | Countryside |
| --- | --- | --- |
| ![Town street](docs/screenshots/town.jpg) | ![Traffic](docs/screenshots/traffic.jpg) | ![Main road through the fields](docs/screenshots/countryside.jpg) |

| Forest | Town square | Headlights |
| --- | --- | --- |
| ![Forest road](docs/screenshots/forest.jpg) | ![The square with the church, the memorial and parked cars](docs/screenshots/square.jpg) | ![Headlights on](docs/screenshots/lights.jpg) |

| Dusk | Night | Headlamps on a village street |
| --- | --- | --- |
| ![The square at 21:30](docs/screenshots/dusk.jpg) | ![A lit street at 23:00](docs/screenshots/night.jpg) | ![The beam on the road at night](docs/screenshots/headlights.jpg) |

| Overcast | Rain | Traffic signals |
| --- | --- | --- |
| ![The square under a solid lid](docs/screenshots/overcast.jpg) | ![Rain on the cobbles](docs/screenshots/rain.jpg) | ![The signalised junction U kaple](docs/screenshots/signals.jpg) |

| Březí | Podhájí | Nové Město |
| --- | --- | --- |
| ![The village of Březí on the main road](docs/screenshots/brezi.jpg) | ![Podhájí on the southern ring](docs/screenshots/podhaji.jpg) | ![The small town of Nové Město](docs/screenshots/mesto.jpg) |

| Wet road | Rainy night |
| --- | --- |
| ![The sky reflected off wet asphalt](docs/screenshots/wetroad.jpg) | ![Headlamps and rain after dark](docs/screenshots/rainynight.jpg) |

All pictures are headless captures from the development container (Xvfb, Mesa llvmpipe
software OpenGL ES 3, no multisampling); a GPU renders the same frames with sharper texture
filtering. Older sets are kept in `docs/screenshots/m10-baseline/` (before Phase 11) and
`docs/screenshots/renderers/` (the same frame on three renderers).

## Status

The initial product milestone, the realism and production-quality phase (Phase 11), the living
world (Phase 12: day and night, weather, traffic signals, four more settlements) and the real
hardware, visual realism and driving polish pass (Phase 13) are all complete. Every feature
listed below exists, runs from a clean checkout, is covered by automated tests where a test is
meaningful, and was verified in screenshots. [`plan.md`](plan.md) is the authoritative task
ledger; nothing in this README claims a feature that `plan.md` does not mark as done.

**All performance figures and every picture in this repository come from a headless container
with no GPU** (Xvfb, Mesa llvmpipe software rasterisation). They describe a software rasteriser,
not a graphics card. [`docs/real-hardware-validation.md`](docs/real-hardware-validation.md) is
the copy-and-paste procedure for producing figures on a real machine; no such run has been
recorded yet.

What you get today:

- One drivable car, the fictional **Lipan 1.2** (a small four-cylinder hatchback defined in
  `content/vehicles/lipan_12.json`), with a project-owned rigid-body, suspension, tyre,
  engine, clutch, manual and automatic gearbox, fuel, thermal and electrical simulation.
- One map, a 6.4 x 7.6 km region around the town of **Lipová**: a cobbled square with the
  church, a memorial column, town houses and parked cars, a prefab estate, shops, a filling
  station on the eastern approach, bus stops and a wayside chapel at the signalised eastern
  junction; residential streets with fenced and planted gardens and cars parked at the kerb.
  Four more places on the roads out of it -- the street village of **Březí**, the village of
  **Podhájí** on the southern ring, the small town of **Nové Město** in the north-east with its
  own square, and the hamlet of **Kamenice** in the western hills -- plus tree avenues, fields,
  meadows and a forest with a gravel forest track. About 27 km of roads, 22 intersections,
  Czech traffic signs and road markings.
- Cockpit camera with a live instrument cluster, rotating steering wheel and a working
  rear-view mirror; exterior chase camera.
- Ambient traffic with Czech registration plates that follows lanes, keeps distance, obeys
  priority, yield, stop, the right-hand rule and working traffic signals, and reacts to the
  player; cars parked on the town square that are as solid as any other obstacle.
- A day and night cycle with a real solar path, street lamps, lit windows and headlamp pools
  after dark, and weather -- clear, scattered cloud, overcast or rain, with wet roads that take
  a third off the grip.
- Physical collisions with buildings, street furniture, trees and traffic cars.
- Procedural engine audio driven by RPM and load, starter, tyre and wind noise, indicators,
  horn, gear and impact sounds.
- Persistent odometer, trip, transmission mode and settings; key bindings configurable in the
  save file; in-game help and debug overlays.

Not included (by design or deferred, see `plan.md` sections 2 and 23): damage, pedestrians,
overtaking traffic, gamepad support, real-brand car models (no legally redistributable Škoda
model was available; the car is procedural).

## Requirements

- CMake 3.23+, Ninja (recommended), a C++23 compiler (GCC 13+, Clang 18+, MSVC 19.38+),
  Python 3 for the tools and static checks.
- Sibling checkouts (the build consumes them with `add_subdirectory()`):
  - `../cna` -- CNA, branch **`next`**, with submodules `third_party/SDL`,
    `third_party/SDL_image`, `third_party/SDL_mixer` and `vendor/googletest`.
  - `../sharp-runtime` -- Sharp Runtime, branch **`next`**.
  - `../easy-gl` and `../meta-gl` when building the OpenGL family renderers (`OPENGLES3`, the
    Linux default, `OPENGL33`, ...).
- Linux packages for SDL3 (X11/Wayland development headers, ALSA/PulseAudio) and OpenGL
  ES/EGL development headers for the GL renderers.

## Building

```bash
git clone --branch next https://github.com/libcna/cna.git
git -C cna submodule update --init third_party/SDL third_party/SDL_image third_party/SDL_mixer vendor/googletest
git clone --branch next https://github.com/libcna/sharp-runtime.git
git clone https://github.com/libcna/easy-gl.git
git clone https://github.com/libcna/meta-gl.git
git clone https://github.com/libcna/cna-car-simulator.git
cd cna-car-simulator
cmake --preset opengles3          # or: default, opengl33, vulkan, software, debug
cmake --build --preset opengles3 -j
ctest --preset opengles3
./build/opengles3/bin/cna-car-simulator
```

Pass `-DCARSIM_CNA_ROOT=/path/to/cna -DCARSIM_SHARP_RUNTIME_ROOT=/path/to/sharp-runtime`
when the checkouts are elsewhere. The renderer is CNA's `CNA_GRAPHICS_RENDERER` option; the
simulator never depends on which renderer was selected and `CNA_CNAEXT` stays `OFF`.

The content directory is copied next to the executable after each build; the source
`content/` directory is used as a fallback during development.

## Running

```
cna-car-simulator [options]
  --width <px> --height <px> --fullscreen   window size (default 1280 x 720)
  --no-audio                                disable the audio stream
  --save <file> | --no-save                 save file location, or run without persistence
  --content <dir>                           content root (vehicles, maps, fonts)
  --vehicle <name> --map <name>             vehicle definition and map to load
  --spawn <name>                            player spawn point: square, forest, fields, east,
                                            kostel, brezi, podhaji, mesto, kamenice
  --route <name> [--route-stay]             drive a named route (town, country, forest) with the
                                            autopilot and exit at its end; --route-stay keeps going
  --quality <tier>                          graphics tier: low, medium or high (default: saved)
  --cockpit                                 start in the cockpit camera
  --help-overlay --debug-overlay            start with an overlay open
  --benchmark [--benchmark-json <file>]     print frame-time statistics at exit (and write JSON)
  --mirror-every <n>                        redraw the rear-view mirror every n frames
  --time <hh:mm> --time-scale <x>           clock the world starts at, and how fast it runs
                                            (60 = a day in 24 minutes, 0 freezes the sky)
  --weather <name>                          clear, cloudy, overcast or rain
  --frames <n> --screenshot <file>          run n frames, save the last one, exit
  --screenshot-cluster <file>               also save the instrument cluster texture
  --lockstep --traffic-warmup <s> --lights  deterministic captures: one sim step per frame,
                                            traffic simulated s seconds before the first frame,
                                            headlights on
  --auto-drive <s>                          scripted start and acceleration for s seconds
  --chase-yaw <deg> --chase-distance <m>    exterior camera framing
  --eye dx dy dz yaw pitch                  cockpit eye offset for inspection captures
  --view x y z hdg pitch                    fixed inspection camera
```

Headless smoke run (Xvfb, dummy audio) as used by the tests and for screenshots:

```bash
scripts/run_headless.sh ./build/opengles3/bin/cna-car-simulator --frames 60 --screenshot shot.png
scripts/capture_set.sh                      # the whole curated set in docs/screenshots/
scripts/benchmark_suite.sh --quick          # the eight benchmark scenes
python3 scripts/benchmark_report.py build/benchmarks --label "this machine"
```

### Measuring and validating

`--route <name>` drives one of the routes defined in `content/maps/lipova/traffic.json` with an
autopilot that follows the lane graph through the ordinary physics -- nothing is teleported -- so a
run is repeatable to the metre. `scripts/benchmark_suite.sh` uses that to run eight deterministic
scenes (clear day, rain, clear night, rainy night, each from the exterior and the cockpit) and
`scripts/benchmark_report.py` turns them into the tables in
[`docs/performance.md`](docs/performance.md), with `--against` for a before-and-after comparison.
`F3` opens the diagnostic overlay: frames per second with the worst 1 %, the update and draw
halves split by stage and by pass, the mirror's cost and resolution, the weather and sun state,
and drawn-against-culled batch counts.

## Controls

Defaults from `Input::InputMapper`; the in-game help (`F1`) always shows the live bindings.

| Action | Keys |
| --- | --- |
| Accelerator / brake while driving | `W` / `S` (also `Up` / `Down`) |
| Steer left / right | `A` / `D` (also `Left` / `Right`) |
| Clutch (manual mode) | `Q` |
| Handbrake | `Space` |
| Engine start / stop (one press; the starter cranks until the engine catches) | `E` |
| Cycle off / turbo / ultra turbo / ultra ultra turbo (about 250 / 400 / 500 km/h) | `O` |
| Gear up / selector up, gear down / selector down | `Left Shift` / `Left Ctrl` (also right-hand keys) |
| Manual gears | `1` .. `6`, `N` neutral, `R` reverse |
| Automatic selector | `P` park, `R` reverse, `N` neutral, `F` drive |
| Toggle automatic / manual | `T` |
| Differential open / limited slip (`LSD` on the HUD) | `U` |
| Indicators left / right, hazard (they switch themselves off when the wheel returns after a turn) | `,` / `.` / `H` |
| Headlights on / off; switch low / high beam | `L`; `K` (also turns on high beam directly) |
| Horn | `B` |
| Camera cockpit / exterior | `C` |
| Toggle full screen | `F11` (or start with `--fullscreen`) |
| Show / hide map | `M` |
| Exhaust smoke on / off | `X` |
| Car / helicopter | `J` |
| Helicopter: forward / back, turn, climb / descend; short / long searchlight | `W` / `S`, `A` / `D`, `Space` / `Q`; `K` |
| Enter walking mode from a stopped car with the engine off; return to car | `G` |
| On foot: forward / back, turn, sidestep, toggle run | `Up` / `Down`, `Left` / `Right`, `A` / `D`, either `Shift` |
| Rear-view mirror on / off, HUD text on / off | `V`, `Tab` |
| Master volume | `Page Up` / `Page Down` |
| Reset vehicle to the road, reset trip meter | `Backspace`, `F5` |
| Clock back / forward an hour, freeze the clock | `F6` / `F7`, `F8` |
| Next weather (clear, cloud, overcast, rain) | `F4` |
| Help overlay, debug overlay, screenshot | `F1`, `F3`, `F12` |
| Quit | `Esc` |

Walking uses a first-person view and audible footsteps at 6 km/h; running is 16 km/h.
The parked car must have its engine off before `G` enters walking mode. `G` cannot
switch to walking from the helicopter. Cars stop for a person on their path, and
the walker cannot move through parked or moving cars.

The running car emits exhaust smoke from its tailpipe. Press `X` to hide or show it;
the choice is saved in the profile.

In ultra ultra turbo, steering remains effective at high speed while the turn
radius widens to keep the car stable. Slow down before a tight corner at very high speed.

Driving notes: the engine has to be started with `E`; the starter only engages in neutral,
with the clutch pressed, or with the automatic selector in `P` or `N`, and it cranks for a
moment before the engine catches. In manual mode releasing the clutch below the stall speed
or at standstill in gear stalls the engine; in automatic mode the car creeps in `D`. The fuel gauge lamp lights on reserve; when
the fuel drops to half of the reserve amount the tank is refilled to 100 % (both fractions are
vehicle-definition parameters, `fuel.refillAtReserveFraction` and `fuel.refillToFraction`).

## Save file and settings

Odometer, trip, transmission mode, the last vehicle and map, and settings are stored as JSON
(schema 1) in `$CARSIM_SAVE_DIR/save.json`, else `$XDG_DATA_HOME/cna-car-simulator/save.json`
(`%APPDATA%\cna-car-simulator\save.json` on Windows, `~/.local/share/...` as the fallback).
The file is written atomically every 30 s and on exit; a file with a newer schema is opened
read-only. Fields have been added over time (the clock and the weather in Phase 12, the graphics
tier in Phase 13) without a schema bump: they are additive with defaults, an older build ignores
keys it does not know, and a test loads a profile written before any of them existed and checks
nothing is lost. Settings and bindings can be edited by hand:

```json
{
  "schemaVersion": 1,
  "odometerKm": 12.4, "tripKm": 3.1, "transmissionMode": "manual",
  "vehicleId": "lipan_12", "mapId": "lipova",
  "settings": {
    "masterVolume": 0.8, "engineVolume": 1.0, "effectsVolume": 1.0,
    "mirrorEnabled": true, "hudVisible": true, "startInCockpit": false,
    "timeOfDayHours": 10.5, "timeScale": 60.0, "weather": "few-clouds",
    "graphicsQuality": "high", "mirrorUpdateEvery": 1
  },
  "bindings": [
    { "action": "Horn", "key": "Enter" },
    { "action": "ToggleCamera", "key": "V" }
  ]
}
```

Action names are the `GameAction` enumerators (`Throttle`, `Brake`, `SteerLeft`,
`SteerRight`, `Clutch`, `Handbrake`, `Horn`, `ToggleEngine`, `ToggleTurbo`, `ShiftUp`, `ShiftDown`,
`GearNeutral`, `GearReverse`, `Gear1` .. `Gear6`, `SelectorPark`, `SelectorDrive`,
`ToggleTransmission`, `ToggleDifferential`, `IndicatorLeft`, `IndicatorRight`, `Hazard`, `Headlights`, `HighBeam`,
`ToggleCamera`, `ToggleFullscreen`, `ToggleMap`, `ToggleFlight`, `ToggleWalk`, `ToggleRun`, `ToggleHelp`, `ToggleDebug`, `Screenshot`, `ResetVehicle`, `ResetTrip`,
`Quit`, `VolumeUp`, `VolumeDown`, `ToggleMirror`, `ToggleHud`); key names are the ones the
help overlay prints (`A`..`Z`, `0`..`9`, `F1`..`F12`, `Space`, `Left Shift`, `Page Up`, ...).
Unknown names are reported as warnings and ignored. An override replaces the default keys of
that action.

## Architecture

```
simulator/
  include/CarSim/, src/
    Core/       command line, JSON reader, noise, curves, save data, version
    Sim/        rigid body, suspension, tyres, engine, clutch, gearboxes, fuel, thermal,
                odometer, electrics, Vehicle facade (120 Hz sub-steps), vehicle definitions
    Map/        map documents (schema v1), road network, lane graph, terrain field,
                object placement, spatial grid, MapWorld (ground queries for the physics)
    Collision/  OBB / cylinder shapes, SAT contacts, impulse response, static world colliders
    Traffic/    lane-following traffic (IDM, priority, deadlock handling), plate generator
    Audio/      engine synthesiser, sound layers, DynamicSoundEffectInstance stream
    Input/      action mapping with overrides
    Render/     world/terrain/road/building/vegetation/sign generators, vehicle renderer,
                cameras, instrument cluster, mirror, sky, bitmap fonts, screenshots
    App/        SimulatorGame (XNA Game subclass), Program
content/        vehicles/*.json, maps/<name>/*.json, fonts/ (generated glyph atlases)
tools/          map generator, map validator, font atlas generator, simulation tracer
scripts/        headless runner, curated capture set, XNA-only static check, asset check
tests/          GoogleTest suites (unit, scenario, soak) and CTest registrations
docs/           framework findings, API boundary, map format, vehicle physics, audio
                design, vehicle materials, cameras, performance notes, renderer
                conformance, real-hardware validation procedure, research (Czech roads,
                plates, assets)
```

Two static libraries keep the renderer-free code testable: `carsim_core` (Core, Sim, Map,
Collision, Traffic, engine synthesis; depends only on CNA's math/core headers and Sharp
Runtime's JSON) and `carsim_render` (everything that touches `GraphicsDevice`, audio devices
and input). The executable is a thin `Game` subclass.

Simulation runs in fixed 120 Hz sub-steps inside `Vehicle::Update`; traffic, collision and
audio mixing run at the frame rate. Coordinates are right-handed, +Y up, north = -Z.

### API boundary

Application code uses CNA only through the XNA 4.0 surface (`Microsoft::Xna::Framework::*`)
plus Sharp Runtime. CNAEXT, renderer implementation classes, renderer-specific includes and
platform libraries are prohibited. `scripts/check_xna_only.py` enforces this as the
`xna_only_api_check` CTest against a list of the XNA 4.0 public types; the reasoning and the
observed renderer behaviours that shaped the code (for example, untextured lit `BasicEffect`
rendering black on the OPENGLES3 renderer, so every lit material is textured) are recorded in
[`docs/api-boundary.md`](docs/api-boundary.md) and
[`docs/framework-findings.md`](docs/framework-findings.md).

Missing XNA functionality is implemented in project code: bitmap fonts drawn with
`SpriteBatch` from pre-rasterised atlases, procedural textures with CPU mip chains, planar
stencil shadows via `Matrix::CreateShadow`, environment-mapped paint through
`EnvironmentMapEffect`, terrain macro lighting baked into a `DualTextureEffect` texture, the
mirror and the instrument cluster as `RenderTarget2D` passes.

### Vehicles are data

A vehicle definition (`content/vehicles/*.json`, schema 1) holds chassis mass, dimensions,
inertia and drag, wheel positions, suspension, tyre (combined-slip, load-sensitive) parameters,
steering geometry, engine (idle/redline/limiter, inertia, friction, torque curve, starter,
thermal and consumption model), clutch, gearbox (ratios, final drive, automatic shift map),
fuel tank and refill rule, indicator period, visual placement (driver eye, steering wheel,
mirror, cluster, paint, plate) and dashboard ranges. `docs/vehicle-physics.md` documents the
model and its compromises; `tools/simtrace` prints acceptance drives.

### Maps are data

Maps are JSON source files with a versioned schema (`map.json`, `terrain.json`, `roads.json`,
`objects.json`, `traffic.json`); road centrelines, intersection patches, lanes, connectors,
conflicts and right of way, terrain conformance, buildings, props, signs and vegetation are
derived deterministically at load time. [`docs/map-format.md`](docs/map-format.md) has the
format decision and schema reference. The sample map is generated by the two-stage pipeline
`tools/maps/build_map.py` and committed as generated output
([`docs/map-generation.md`](docs/map-generation.md)); `carsim-mapvalidate` (CTest
`map_validate_lipova`) validates any map, and CTest `map_regeneration_check` fails if the
committed map and its generator drift apart.

### Traffic

Cars follow lane and connector polylines with the Intelligent Driver Model, respect speed
limits and curve speeds, indicate before turns, come to a full stop at stop signs, apply
priority / yield / right-hand rule and left-turn-yields-to-oncoming from the lane graph,
accept gaps by time to arrival, keep junction boxes clear, and treat a car whose body lies on
their path as an obstacle. A deadlock breaker releases the longest-waiting car when nothing
moves, and a nose-to-nose stand-off inside a junction is resolved by the yielding car backing
out to its line. Plates come from a generator that follows the Czech `1A2 3456` series with
regional letter weights (`docs/research/czech-plates.md`). A traffic car queues behind a
player who stops in the lane; it does not overtake.

Traffic continues beneath an airborne helicopter; the helicopter still collides with car
bodies when it actually touches them.

## Testing

```bash
ctest --preset opengles3                  # everything below
ctest --preset opengles3 -L unit          # GoogleTest suites (no display needed)
ctest --preset opengles3 -L static        # XNA-only API check, asset manifest check
ctest --preset opengles3 -L content       # sample map validation and map-generation consistency
ctest --preset opengles3 -L display       # headless smoke run through scripts/run_headless.sh
```

The unit binary `carsim_tests` covers the vehicle model (engine states, clutch stall, gear
logic, fuel refill rule, thermal, odometer, determinism, acceleration and braking bands, a
constant-speed fuel cycle), wet braking, cornering and acceleration measured against dry, map
loading and validation, road and lane graph geometry (turn types, right of way, antisymmetric
yields, routes), terrain/ground agreement over every lane and junction connector, collision
shapes and scenarios, traffic (IDM, following, intersections, spawning, a 30-minute soak with no
body overlaps or stuck cars and a 10-minute soak at the signalised junction with no red-light
crossings), the autopilot driving every benchmark route of the shipped map end to end, plates,
engine synthesis, graphics tiers and save data, including a profile written by an earlier phase.

Screenshots were reviewed for every rendering change; the headless workflow is
`scripts/run_headless.sh <binary> --frames N --screenshot out.png [--view ...|--cockpit|--auto-drive s]`.

## Performance

Everything measured here is the development container: Mesa llvmpipe **software** rendering, four
threads, no GPU. The update half of a frame stays below 0.5 ms (vehicle physics 0.19 ms,
collision 0.01 ms, traffic AI 0.18 ms, audio 0.001 ms) at every world size the map reaches; all
the cost is in the draw half, which on a software rasteriser is rasterisation rather than
submission. The cockpit's rear-view mirror was the largest single lever -- a second full world
pass into a 200-pixel strip -- and capping its draw distance at 300 m took it from 58.9 ms to
45.5 ms. Three graphics tiers (`--quality low|medium|high`, `settings.graphicsQuality`) trade
draw distance, vegetation distance and mirror rate; `high` is the default and is what every
picture and table was taken at. Per-scene tables, the before-and-after comparisons and the
commands that reproduce them are in [`docs/performance.md`](docs/performance.md);
`--benchmark-json` writes them as JSON. The same scenes were built and compared on the
OPENGLES3, OPENGL33 and SOFTWARE renderers
([`docs/renderer-conformance.md`](docs/renderer-conformance.md)), and
[`docs/real-hardware-validation.md`](docs/real-hardware-validation.md) is the procedure for a
first run on a real PC with a GPU.

## Licensing and provenance

Source code, documentation and project-authored data: MIT (see [`LICENSE`](LICENSE)). CNA is
licensed under the Microsoft Public License (Ms-PL); Sharp Runtime under its own licence; both
are separate dependencies that are not vendored here.

All textures, meshes, sounds, plate and sign faces are generated by project code. The only
external asset is the **D-DIN** font family (Datto Inc., SIL Open Font License 1.1), used for
the HUD, the instrument cluster and plate characters; its licence text, checksums and
attribution are in [`assets/ASSETS.md`](assets/ASSETS.md) and `assets/manifest.json`, checked
by `scripts/check_assets.py`. Sources that were evaluated and rejected (no verifiable licence,
unsuitable content or unreachable hosts) are listed there as well.
