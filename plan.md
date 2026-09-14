# cna-car-simulator -- engineering plan and task ledger

This file is the authoritative plan for the project. Every task has an ID, a status and
acceptance criteria. Statuses: `[ ]` open, `[~]` in progress, `[x]` done (verified, not merely
skeleton code), `[-]` deferred (with reason). Update this file in the same commit as the work.

Last synchronised with the repository: 2026-09-14 (M5 collision and M6 traffic complete except TRF-007/008; ENV-007, ENV-008, UI-001, RND-009 and SIM-019 open).

---

## 1. Goals

- A serious, realistic 3D passenger-car driving simulator in C++23 on the XNA 4.0-compatible
  public API of CNA (branch `next`) and Sharp Runtime (branch `next`).
- Drive believable passenger cars through a fictional but convincingly Czech environment:
  town streets, intersections, residential area, a through-road, countryside, fields, forest,
  a minor forest road.
- Start, operate and drive the car with realistic controls: engine start/stop, automatic or
  manual transmission with a real clutch, indicators, lights, horn, handbrake, two cameras,
  a functional instrument cluster, a rear-view mirror, dynamic engine audio.
- Meet AI traffic that follows lanes on the right, yields at intersections, reacts to the
  player and carries valid-looking Czech plates.
- Collide physically with vehicles and static geometry.
- Architecture that can grow for years: data-driven vehicles, reusable map format, explicit
  lane graph, modular systems, tests, debug tools, strict API boundary.

## 2. Non-goals (initial project)

Missions, jobs, deliveries, economy, career, money, dealerships, buying cars, multiplayer,
pedestrians, damage-repair economy, police, weather, seasons, day/night cycle, fuel stations,
enormous open world, VR, mandatory steering-wheel hardware support, visual damage (optional
later, separate from the collision solver).

## 3. Framework and API boundary

See `docs/framework-findings.md` (what CNA/Sharp Runtime actually provide) and
`docs/api-boundary.md` (Tier A = XNA 4.0 API + Sharp Runtime, Tier P = this repository,
Tier C = prohibited CNAEXT/internals/renderer specifics). Enforced by
`scripts/check_xna_only.py` (CTest `xna_only_api_check`) and `CNA_CNAEXT=OFF` in CMake.

Key consequences that shape the design:

1. **No custom shaders.** `Effect` needs compiled D3D9 bytecode and a renderer flag; no HLSL
   compiler is available. Rendering uses `BasicEffect`, `DualTextureEffect`,
   `AlphaTestEffect`, `EnvironmentMapEffect`, `SpriteBatch`, render targets, states and
   `DrawInstancedPrimitives`.
2. **Vertex layouts are selected by stride** (16/20/24/32/40/52 bytes). Only XNA vertex structs
   and the 40-byte dual-UV layout are used.
3. **Shadows**: baked static lighting (sun visibility + ambient occlusion) through the dual-texture
   layout, plus planar projected stencil shadows (`Matrix::CreateShadow`) for vehicles.
4. **Text**: project-owned bitmap font atlas drawn with `SpriteBatch`.
5. **Audio**: fully synthesised in project code, streamed through `DynamicSoundEffectInstance`.
6. **Assets**: procedural generation for most content (license-clean); external assets only from
   sources reachable and verifiable (GitHub-hosted, per-item licence), recorded in
   `assets/manifest.json`.

## 4. Architecture

### 4.1 Libraries and directories

```
cna-car-simulator/
  CMakeLists.txt, CMakePresets.json, cmake/        build (CNA + Sharp Runtime as siblings)
  simulator/include/CarSim/<Module>/*.hpp          public headers per module
  simulator/src/<Module>/*.cpp                     implementation
    Core/      command line, version, logging, RNG, math helpers, JSON access, units
    Sim/       vehicle definition, engine, clutch, transmissions, drivetrain, tyres, suspension,
               rigid body, fuel, thermal, odometer, electrics (lights/indicators), Vehicle facade
    Map/       map documents (JSON), road network, lane graph, terrain, objects, validator,
               geometry generators (road/terrain/building/vegetation meshes), spatial grid
    Collision/ collision world, shapes, contact generation, impulse solver, ground queries
    Traffic/   traffic system, traffic vehicles, route planner, intersection manager, spawner,
               plate generator
    Audio/     engine synthesiser, sound layers, mixer
    Input/     input mapping (actions <-> keys), driver controls
    Persistence/ settings and save data (versioned JSON)
    Render/    renderer, cameras, world/vehicle/cockpit renderers, instrument cluster, mirror,
               shadows, sky, procedural textures, bitmap font, debug overlay, screenshot
    App/       SimulatorGame (glue), help overlay, main
  tools/        C++ tools (map validator) and Python tools (font atlas, asset fetch, checks)
  tests/        GoogleTest unit/integration tests
  content/      runtime data: vehicles/*.json, maps/*/, fonts/, textures/ (generated + fetched)
  assets/       asset sources, manifest and provenance (ASSETS.md, manifest.json)
  docs/         findings, research, architecture, format docs
  scripts/      static checks, headless run helpers
```

CMake targets: `carsim_core` (Core, Sim, Map, Collision, Traffic, Input, Persistence, Audio
synthesis maths; depends on CNA math headers + Sharp Runtime only), `carsim_render`
(Render, Audio playback; depends on the CNA umbrella), `cna-car-simulator` (App), `carsim_tests`,
tools.

### 4.2 Coordinate system and units

Right-handed XNA frame, +Y up, metres, seconds, kilograms, radians internally (degrees only in
data files where noted). Vehicle local frame: +X right, +Y up, -Z forward (XNA `Vector3::Forward`).
Map plane is XZ; "north" is -Z. Headings are yaw around +Y, counter-clockwise positive when seen
from above.

### 4.3 Simulation stepping

`Game::IsFixedTimeStep = true`, 60 Hz `Update`. Inside each update the vehicle physics runs two
substeps of 1/120 s (`Sim::FixedStepper`), collision resolution runs per substep, traffic
decision logic runs at 20 Hz with per-substep integration, audio synthesis runs on the audio
buffer cadence (~23 ms blocks). Rendering interpolates nothing (60 Hz is the frame rate target;
`IsRunningSlowly` is shown in the debug overlay).

### 4.4 Data flow per frame

`Input::InputMapper` -> `Sim::DriverControls` -> `Sim::Vehicle::Step` (player) and
`Traffic::TrafficSystem::Step` (AI) -> `Collision::CollisionWorld::Resolve` -> state snapshots
-> `Render::Renderer::DrawFrame` (mirror pass, main pass, cockpit, HUD) + `Audio::VehicleAudio`.

## 5. Vehicle architecture

- `VehicleDefinition` (data, JSON in `content/vehicles/<id>.json`, validated on load): dimensions,
  mass, centre of gravity, inertia, wheel positions/radius/width, steering limit and ratio,
  suspension (spring rate, damping, travel), tyre parameters (peak friction, stiffness, load
  sensitivity), engine (idle/redline RPM, torque curve points, inertia, friction, starter
  parameters, thermal constants), clutch (max torque, engagement curve), gearbox (type, ratios,
  reverse, final drive, automatic shift map), brakes (max torque front/rear, handbrake torque),
  aerodynamics (Cd, frontal area, rolling resistance), fuel (tank capacity, reserve litres,
  refill fraction, consumption parameters), electrics (indicator period), model (mesh
  generator parameters or model file + node mapping), audio set parameters, dashboard mapping.
- `Vehicle` facade composes `Engine`, `Clutch`, `Transmission` (`ManualTransmission`,
  `AutomaticTransmission`), `Drivetrain` (open differential), four `Wheel`s with `TyreModel`
  and `Suspension`, `RigidBody`, `FuelSystem`, `EngineThermal`, `Odometer`, `Electrics`.
- `VehicleState` snapshot for rendering/audio/traffic: pose, wheel poses, steering wheel angle,
  lamp states, gauge values.

## 6. Vehicle physics model

Rigid body with 6 degrees of freedom (position, quaternion orientation, linear and angular
velocity, mass, diagonal inertia). Four wheels attached at defined positions; each wheel is a
ray/segment cast against the ground surface (terrain + road surface + static collision) with a
spring-damper suspension producing the normal load. Tyre forces come from a simplified
Pacejka-style magic formula in longitudinal (slip ratio) and lateral (slip angle) directions
with a friction-circle combination and load sensitivity; at low speed a relaxation/damping term
prevents jitter. Wheel angular velocity is integrated from drive torque, brake torque and tyre
reaction. Drive torque comes from the drivetrain (engine -> clutch -> gearbox -> final drive ->
open differential). Aerodynamic drag and rolling resistance act on the body; slopes work through
the normal loads and gravity. Handbrake applies rear brake torque. Collisions apply impulses to
the body (section 9). Documented compromises: no camber/toe, no anti-roll bars beyond a roll
stiffness term, rigid-axle-free independent suspension approximation, no tyre thermal model.

Stability rules: fixed 120 Hz substeps, semi-implicit Euler, clamped slip quantities, tyre force
limited by available friction, suspension force clamped to prevent launch, body sleeps when
stationary. Determinism: all simulation code uses floats deterministically with no threads.

## 7. Engine, transmission, fuel, thermal, odometer

- Engine states: `Off`, `Starting` (starter cranking ~0.6--1.2 s with RPM climbing to catch
  speed), `Running`, `Stalled` (RPM collapsed under load, returns to `Off` after a moment).
  `E` toggles start/stop. Torque = curve(RPM) * throttle minus friction/pumping losses; idle
  controller adds throttle at idle; rev limiter at redline; engine braking when throttle closed.
- Clutch: torque capacity scaled by pedal position; slip when engine torque exceeds capacity;
  lock-up when speeds match; manual stall when clutch engaged at low RPM under load.
- Manual gearbox: ratios from data; neutral; reverse; shifting requires clutch (else grinding
  refusal); shift up/down keys and direct selection.
- Automatic gearbox: shift map from speed/RPM/throttle with hysteresis and kick-down; P/R/N/D
  selector; creep; internal clutch model prevents stall.
- Fuel: consumption = f(RPM, load, displacement) using a brake-specific-fuel-consumption style
  map + idle rate; tank, reserve threshold (warning lamp), automatic refill to 100 % when fuel
  falls to `reserve * refillFraction` (default 0.5) -- all configurable.
- Thermal: lumped coolant temperature with heat input from load, radiator/thermostat cooling
  towards 90 °C operating range, cooling when off; gauge reads the simulated value.
- Odometer: integrates body speed along the ground (not wheel RPM), persists total and trip.

## 8. Rendering strategy

- Fixed late-morning summer lighting: sun elevation ~48°, azimuth from the south-east, warm
  key light, blue-grey sky ambient, mild distance haze (fixed fog, not weather).
- Static world: terrain and roads use the 40-byte dual-UV layout with `DualTextureEffect`:
  texture 1 = tiled albedo (procedural asphalt/grass/gravel/paving), texture 2 = baked
  world-space lightmap (sun visibility + AO + N·L) at 0.5 m/texel in chunks. Buildings, props
  and signs use `DualTextureEffect` with per-object lightmaps (per-vertex lighting through a
  gradient texture where coarse is acceptable) or `BasicEffect` where dynamic lighting matches.
- Vehicles: `BasicEffect` per-pixel lighting; car paint through `EnvironmentMapEffect` with a
  procedural sky cube map; glass alpha-blended; lamps emissive; planar stencil shadows.
- Vegetation: `AlphaTestEffect` crossed quads with procedural leaf-cluster textures, instanced
  through `DrawInstancedPrimitives`; trunks `BasicEffect`.
- Sky: gradient dome (`VertexPositionColor`) + sun disc + procedural clouds.
- Cockpit: 3D interior mesh, steering wheel rotating with the steering angle, instrument
  cluster with needle meshes, lamp quads and a small `RenderTarget2D` digital display,
  rear-view mirror quad textured by a `RenderTarget2D` (reduced resolution, flipped UVs).
- Culling: frustum culling on chunk and object bounds; distance culling by class; LOD for
  vegetation (billboard far LOD) and props; traffic simplified beyond a radius.
- Text/HUD: bitmap font atlas (D-DIN, OFL) generated by `tools/fontatlas.py`, drawn with
  `SpriteBatch`.
- Debug overlays toggled by keys (never shown by default).

## 9. Collision system

- `CollisionWorld` with a uniform grid over the map (cells 25 m) holding static colliders
  (oriented boxes and convex prisms for buildings/walls/posts/trees/kerbs) and dynamic
  vehicle bodies (oriented boxes).
- Detection: OBB-OBB separating axis test with contact manifold (deepest points), OBB vs
  convex prism, wheel/ground ray casts.
- Response: sequential impulses with restitution (low, 0.1--0.2), Coulomb friction, angular
  effects through contact arms, positional correction (Baumgarte-style) to avoid sinking;
  masses drive the outcome (a 1.1 t car bumping a 1.9 t van moves less). Different outcomes for
  bumper taps, side impacts, head-on and static clipping fall out of the impulse solver.
- Traffic vehicles switch from lane-following to physical response for a recovery period after
  impact, then re-acquire their lane or despawn when far from the player.

## 10. Traffic AI

- Lane graph from the map (section 11); routes planned with A* over lane links; spawn points and
  destinations from `traffic.json`; density and radius scaling.
- Following: Intelligent Driver Model (desired speed = min(speed limit, curvature limit),
  time headway, minimum gap, comfortable deceleration) against the nearest leader in the lane
  chain (including the player's vehicle when it occupies the lane ahead).
- Intersections: priority rules from map data (main road, yield/stop, right-hand rule); a gap
  acceptance model on conflicting connectors; stop lines; simple signal support in the data
  model (deferred implementation unless the sample map needs it).
- Lateral: vehicles track their lane centreline with a pure-pursuit controller; lane geometry
  gives yaw and terrain gives height/pitch/roll.
- Reaction to the player: treated as an obstacle in the lane (IDM leader) and in conflict areas.
- Plates: `PlateGenerator` (section 13) assigns a plate at spawn; textures baked into an atlas.

## 11. Map architecture

See `docs/map-format.md`. JSON source (`map.json`, `terrain.json`, `roads.json`,
`objects.json`, `traffic.json`, optional `audio.json`), `schemaVersion` per file, loader with
version checks, validator tool + tests. Runtime structures: `RoadNetwork`, `LaneGraph`,
`TerrainField`, `MapObjects`, `SpawnPoints`; generators produce render meshes, lightmaps and
collision shapes deterministically. Spatial grid for objects, colliders, lanes and traffic.

## 12. Audio

Project-owned synthesis (`Audio::EngineSynth`): additive harmonics at the firing frequency
(RPM/60 * cylinders/2) with load-dependent harmonic balance, intake/exhaust layers, mechanical
noise, sub rumble; starter crank, catch and shutdown transitions; parameters smoothed per sample
to avoid discontinuities. Other layers: tyre/road noise (speed-dependent filtered noise), wind,
brake squeal (light), indicator relay tick-tock, horn (two-tone), collision impacts scaled by
impulse, gear engagement clunk. All streamed through `DynamicSoundEffectInstance` at 44.1 kHz.
No external sound files, so provenance is trivial; the design is documented in
`docs/audio-design.md`.

## 13. Czech registration plates

Rules in `docs/research/czech-plates.md`. Generator produces standard `1A2 3456` plates with
regional weighting, two-letter series for A/B/T/U, forbidden letters excluded, optional EL
plates; textures rendered procedurally (520:110, EU band, DIN-like font). Tests validate format,
excluded letters, uniqueness, distribution and rendering layout metrics.

## 14. Asset pipeline and licensing

- Default: procedural generation (car, buildings, props, signs, textures, sounds). Everything
  generated is MIT-licensed project output.
- External assets only from per-item-licensed, reachable sources (GitHub-hosted repositories);
  fetched by `tools/fetch_assets.py` with SHA-256 verification into `content/` and recorded in
  `assets/manifest.json` (title, author, source URL, licence, retrieval date, files, hashes,
  modifications, attribution text). `assets/ASSETS.md` is generated from the manifest.
  Rejected/evaluated assets never enter git.
- Model import path (for future glTF vehicles): `tools/model_prepare.py` normalises coordinate
  system/scale/orientation, triangulates, names nodes; `cna-content` converts glTF to CNB; the
  runtime replaces part effects with stock effects using a per-vehicle node mapping JSON
  (wheels, steering wheel, lamps, dashboard needles, attachment points). Documented in
  `docs/asset-pipeline.md`.
- First vehicle decision: no legally redistributable Škoda model was obtainable (Sketchfab is
  account-gated and blocked; licences unverifiable). The first car is a **procedurally generated
  fictional compact hatchback** ("Lipan 1.2"), parametrised by realistic B-segment data, with
  a full cockpit. The data-driven vehicle system and the glTF path make adding a licensed
  Škoda later a data task.

## 15. Persistence

`Persistence::SaveData` versioned JSON in the user data directory (`$XDG_DATA_HOME` or
`~/.local/share/cna-car-simulator/`, `%APPDATA%` on Windows): selected car, transmission mode,
odometer total, trip odometer, control bindings, graphics/audio preferences. Written on exit and
every 30 s; corrupt files are backed up and defaults restored.

## 16. Testing strategy

- Unit tests (GoogleTest, `carsim_tests`): gear ratios and shift decisions, clutch model,
  torque interpolation, fuel consumption and reserve/refill thresholds, odometer, thermal,
  engine state machine, tyre model monotonicity, rigid body integration, fixed stepper,
  steering geometry, plate generator, lane graph connectivity, routing, map validation, IDM
  following, collision math (SAT, impulses), save/load round trips, input mapping, command line.
- Simulation acceptance tests: scripted drives (0--100 km/h time, braking distance, stall test,
  hill hold, fuel use over a fixed cycle) with tolerance bands.
- Integration/smoke: `simulator_smoke` (frames), headless screenshot captures under Xvfb
  inspected during development, traffic soak test (N minutes, no overlaps/crash counters).
- Static: `xna_only_api_check`, map validator over `content/maps`, asset manifest validator.

## 17. Performance strategy

Budget: 60 Hz at 1280x720 on a mid-range GPU; headless llvmpipe only for correctness.
Measures: chunked static batches (one draw per chunk per material), frustum + distance culling,
vegetation instancing and billboard LOD, mirror at 1/4 resolution updated every frame (or every
2nd frame configurable), traffic simulation radius and simplified far vehicles, spatial grid
queries, no per-frame allocations in hot paths, in-app frame-time overlay, `--benchmark` mode
that logs frame statistics for a scripted camera path.

## 18. Milestones

| ID | Milestone | Exit criteria |
| --- | --- | --- |
| M0 | Skeleton | builds against CNA next + Sharp Runtime next; static check; smoke test; first push |
| M1 | Vehicle core | engine/clutch/gearboxes/fuel/thermal/odometer/rigid body/tyres with tests and acceptance drives |
| M2 | Rendering base | world renderer with lit test terrain, procedural car mesh drawn, chase + cockpit cameras, bitmap text HUD, screenshots verified |
| M3 | Map + roads | map JSON schema, loader, validator, lane graph, road/terrain geometry, sample map skeleton driveable |
| M4 | Cockpit | dashboard cluster with live gauges/lamps, steering wheel, mirror, lights, indicators, horn |
| M5 | Collision | static and vehicle collisions with impulse response, tests, drive verification |
| M6 | Traffic | routes, following, intersections, spawning, plates, reaction to player, soak run |
| M7 | Audio | engine synthesis and layers verified across start/idle/rev/shift/shutdown |
| M8 | Environment | Czech town/countryside/forest content, signs, vegetation, buildings, baked lighting, shadows |
| M9 | Persistence/UX | settings, save data, help overlay, debug overlays, input mapping config |
| M10 | Polish/audit | performance pass, README, licence audit, final audit checklist |

## 19. Task ledger

### M0 Skeleton
- [x] `SKEL-001` Inspect CNA next and Sharp Runtime next; record findings (`docs/framework-findings.md`).
- [x] `SKEL-002` Define the API boundary and write the static checker + XNA 4.0 type list.
- [x] `SKEL-003` Map format decision documented (`docs/map-format.md`).
- [x] `SKEL-004` Research notes: Czech plates, roads, assets (`docs/research/`).
- [x] `SKEL-005` CMake skeleton consuming CNA/Sharp Runtime as siblings; presets; warnings.
- [x] `SKEL-006` Minimal `SimulatorGame` with fixed timestep, test scene, `--frames`, `--screenshot`.
- [x] `SKEL-007` First full build (OPENGLES3 under Xvfb/llvmpipe), unit tests, static check and a 30-frame headless run with screenshot pass.
- [x] `SKEL-008` README, LICENSE, .gitignore, plan.md; first commit pushed.

### M1 Vehicle core (`Sim`)
- [x] `SIM-001` `VehicleDefinition` JSON schema + loader + validation with tests.
- [x] `SIM-002` Torque curve (`Curve` interpolation) + engine friction model with tests.
- [x] `SIM-003` Engine state machine (Off/Starting/Running/Stalled), starter, idle control, limiter.
- [x] `SIM-004` Clutch model (capacity, slip, lock-up) with tests.
- [x] `SIM-005` Manual transmission (ratios, neutral, reverse, shift rules) with tests.
- [x] `SIM-006` Automatic transmission (shift map, hysteresis, kick-down, P/R/N/D, creep) with tests.
- [x] `SIM-007` Drivetrain (final drive, open differential, inertia) with tests.
- [x] `SIM-008` Tyre model (combined slip, load sensitivity, low-speed damping) with tests.
- [x] `SIM-009` Suspension ray casts + spring/damper with clamps; tests.
- [x] `SIM-010` Rigid body integration (6-DoF, inertia, gravity, drag, rolling resistance) with tests.
- [x] `SIM-011` Brakes and handbrake; steering geometry (Ackermann approximation, rate limits, ratio).
- [x] `SIM-012` Fuel system (BSFC-style consumption, reserve lamp, configurable 50 % refill) with tests.
- [x] `SIM-013` Engine thermal model with tests.
- [x] `SIM-014` Odometer (total/trip, ground speed integration) with tests.
- [x] `SIM-015` Electrics: indicators (blink period, hazard), lights, brake/reverse lamps, horn state.
- [x] `SIM-016` Vehicle facade + `VehicleState`; fixed 120 Hz sub-stepping inside `Vehicle::Update`; determinism test.
- [x] `SIM-017` Acceptance drives: 0--100 km/h (8--18 s band, measured ~14 s), braking 100--0 (36--60 m, measured 40 m), clutch stall, hill hold, straight-line stability, steering direction, odometer/fuel response; `tools/simtrace` prints traces. A constant-speed fuel-cycle measurement (L/100 km at 50 and 90 km/h) is still open (SIM-019).
- [x] `SIM-018` First vehicle definition `lipan_12.json` with documented parameter rationale.
- [ ] `SIM-019` Constant-speed fuel-cycle test (50/90 km/h cruise L/100 km bands) and consumption tuning.
- [x] `SIM-020` Physics model documented with compromises (`docs/vehicle-physics.md`).

### M2 Rendering base (`Render`)
- [x] `RND-001` Frame orchestration in `SimulatorGame` (sky, ground, vehicle opaque/transparent, HUD); `CameraPose` with view/projection/frustum.
- [x] `RND-002` `MeshData` builder (box, cylinder, torus, loft, smooth normals, winding tests) and `GpuMesh` for the XNA vertex structs + 40-byte dual-UV layout.
- [x] `RND-003` Procedural texture generator (asphalt, grass, gravel, soil, paving, plaster, roof tiles, bark, leaves, sky cube, clouds) + CPU mip chain upload. PNG cache deferred (generation is fast enough; PERF-004).
- [x] `RND-004` Bitmap font tool (`tools/fontatlas.py`, premultiplied atlases, `tools/generate_fonts.sh`) + `BitmapFont` renderer via `SpriteBatch` (UTF-8, Czech glyphs, shadowed text, built-in fallback).
- [x] `RND-005` Procedural car mesh generator (`ProceduralCar`: lofted hatchback body classified per loft quad into paint/glass/trim, wheels, lamps, mirrors, plates, interior shell, dashboard, cluster, steering wheel, seats) from definition data; part roles drive animation. Body-shape polish tracked as UX-006.
- [x] `RND-006` `VehicleRenderer`: wheel spin/steer/suspension from `VehicleState`, steering wheel rotation, needle poses, emissive lamps, environment-mapped paint with sun glint (cube map alpha mask), separate interior lighting.
- [x] `RND-007` Chase camera (smoothed yaw/position, speed pull-back, `--chase-yaw`/`--chase-distance` framing) and cockpit camera (driver eye from data, subtle lateral sway). Collision-aware distance moves to COL-006.
- [x] `RND-008` Sky dome + sun billboard + cloud layer; fixed lighting rig constants in `LightingRig` (documented in section 8).
- [ ] `RND-009` Renderer conformance probe at start-up (dual-UV layout, instancing) with graceful fallback and log.
- [x] `RND-010` HUD and debug overlay (`F3`): frame time, speed, RPM, gear, engine state, pedals, clutch lock, fuel, coolant; culling counts follow with the world renderer (ENV-008).
- [x] `RND-011` Screenshot verification workflow under Xvfb (`scripts/run_headless.sh`, `--frames/--screenshot/--auto-drive/--cockpit`); screenshots inspected for every rendering change.

### M3 Map and roads (`Map`)
- [x] `MAP-001` Map JSON schema v1 (`docs/map-format.md`) + loader (`MapDocument`) with version checks, dotted-path error reporting and structural validation; shared `Core::JsonReader`.
- [x] `MAP-002` `RoadNetwork`: straight-and-arc centrelines with corner fillets, cross-sections, urban speed limits, terrain-following heights pinned to nodes, intersections with setbacks, kerb fillets and sloped junction planes, road pieces, spatial grid queries, surface height with crown.
- [x] `MAP-003` `LaneGraph`: lanes per direction and piece, Hermite connectors with turn types, conflicts, yield lists (priority, right-hand rule, left turn yields to oncoming), dead-end U-turns, nearest lane, Dijkstra routes, reachability; tests.
- [x] `MAP-004` `TerrainField`: procedural base (signed fBm + hills/ridges/plateaus), region classification, road conformance with blend zone, bilinear height/normal; `MapGround` composite raycast used by the vehicle; tests.
- [x] `MAP-005` Road geometry generator (`RoadMeshBuilder`: crowned surface, shoulders, kerbs, sidewalks on urban stretches, centre/edge lines V 1a/V 2a/V 2b/V 4, stop bars V 5, give-way triangles V 6a, intersection fan patches) built from the same height functions the physics uses. Pedestrian crossings (V 7) and sidewalk corners at junctions remain in ENV-007.
- [x] `MAP-006` Terrain chunks (32 x 32 cells, dual-UV) with a baked macro texture (region tint, sun lighting, verge darkening) drawn with `DualTextureEffect`; terrain sits 12 cm under the roads with a 16 m blend; frustum culling per chunk.
- [x] `MAP-007` Static objects: `ObjectPlacement` resolves buildings, props, signs, vegetation (forests, avenues, single trees) and delineators onto the terrain with road/building margins; grids for collision and culling.
- [x] `MAP-008` `SpatialGrid` (uniform cells) used for road segments, intersection patches and lane points; objects/colliders follow in M5/M8.
- [x] `MAP-009` `carsim-mapvalidate` tool (+ `map_validate_lipova` CTest): structural checks, dead ends, reachability, grades, spawn placement, statistics.
- [x] `MAP-010` Sample map "Lipová" v1: road network (17 km, 13 intersections), terrain, regions, 356 buildings, signs, forests, avenues and spawns authored by `tools/maps/generate_lipova.py`, validated, loaded by the simulator (`--map`, `--spawn`) and driven on; screenshots of the square, the church junction and the eastern approach reviewed. Buildings, vegetation, signs and props are drawn in M8.

### M4 Cockpit and controls (`Render`, `Input`)
- [ ] `UI-001` Input mapper with action bindings: `InputMapper` with default keyboard bindings and action enumeration is in place (M2); JSON overrides and the gamepad path follow in M9 (`UX-002`).
- [x] `UI-002` Instrument cluster rendered into a `RenderTarget2D` with `SpriteBatch` (`InstrumentCluster`): speedometer and tachometer with tick marks, numerals (D-DIN condensed) and red zone, fuel and coolant gauges, odometer/trip display, gear and mode, consumption; ignition-off state; `--screenshot-cluster` dump for review.
- [x] `UI-003` Cluster lamps with procedural icons: indicators, low/high beam, handbrake, coolant, low fuel, battery, engine; `InstrumentCluster::LampLit` is the single source of the lamp logic.
- [x] `UI-004` Steering wheel rotates with the simulated steering-wheel angle (ratio from the vehicle definition); interior shell, door cards, dashboard, hooded binnacle, seats and mirror visible in the cockpit view.
- [x] `UI-005` Rear-view mirror: `MirrorView` renders sky, world and vehicle exterior into a 768 x 200 `RenderTarget2D` from the mirror position with a horizontally flipped projection (clockwise culling), only while the cockpit camera is active. Resolution/rate settings arrive with the settings file (M9).
- [x] `UI-006` Exterior lamps: headlight, brake, reverse and indicator/hazard parts switch emissive colour from `VehicleState`.
- [x] `UI-007` Camera switch (`C`) toggles cockpit/chase immediately (the chase camera snaps on first use); help overlay (`F1`) lists the bindings; `F3` debug overlay; `F12` screenshot.

### M5 Collision (`Collision`)
- [x] `COL-001` Shapes: oriented boxes and vertical cylinders with SAT (15 axes) and segment-box contact generation (`Collision/Shapes`); unit tests for separation, penetration, rotated boxes and cylinders. Convex prisms were not needed: buildings are boxes; the terrain is handled by the suspension raycasts.
- [x] `COL-002` Impulse response with restitution, Coulomb friction and angular terms (world inverse inertia), split positional correction with slop and per-resolve cap; contact events (point, normal, impulse, closing speed, collider kind) for audio and the debug overlay.
- [x] `COL-003` Static colliders from `ObjectPlacement`: buildings, walls, fences, shelters, benches, timber, posts, lamps, signs, tree trunks and map boundary walls (44k+ on the sample map) in a spatial grid; delineators stay soft. Kerbs are not colliders (the suspension rides over the 12 cm step).
- [ ] `COL-004` Vehicle-vehicle collision: `ResolveVehiclePair` (two physics bodies) and `ResolveVehicleAgainstBox` (player against a traffic car treated as a moving box with mass) are implemented and tested; the traffic integration lands with M6.
- [x] `COL-005` Scenario tests: wall stop from 43 km/h (no more than 6 cm penetration), offset post impact induces yaw, head-on pair separates with bounded momentum error, sample-map spawns are clear of colliders.

### M6 Traffic (`Traffic`)
- [x] `TRF-001` Route search over the lane graph (Dijkstra in `LaneGraph::FindRoute`, tested); ambient traffic picks its next link with straight-through preference (`RandomLink`).
- [x] `TRF-002` `TrafficVehicle` follows lane and connector polylines kinematically (path parameter, steer angle from curvature) with Intelligent Driver Model car following, speed limits, curve speeds and look-ahead braking; tests (free road, follower keeps distance and matches speed).
- [x] `TRF-003` Intersection behaviour from the lane graph's conflict/yield lists: priority, yield, stop (full stop at the line), right-hand rule, left turn yields to oncoming, time-gap acceptance, exit-blocked check, deadlock breaker; test: minor road waits for main-road traffic.
- [x] `TRF-004` Spawner/despawner around the player (distance ring, outside the view cone, lane spacing, `maxVehicles` from `traffic.json`), despawn beyond `despawnDistance`; test on the sample map.
- [x] `TRF-005` Player interaction: the player is projected onto the lane graph and acts as leader/obstacle, is respected in gap acceptance, and collides with traffic cars through `ResolveVehicleAgainstBox` (the AI car stops for a few seconds after a hit).
- [x] `TRF-006` `PlateGenerator` (standard `1A2 3456` series with regional weights and two-letter series, optional `EL` plates, validation, uniqueness, seeding) with tests; `PlateRenderer` draws 520 x 110 plates with the EU band, stars, `CZ` and D-DIN Bold characters; the player's plate comes from the vehicle definition.
- [ ] `TRF-007` Traffic vehicle variants: eight paint colours and per-driver speed factors are in; body variants (sedan, van) need additional vehicle definitions (deferred, see section 23).
- [ ] `TRF-008` Soak test (30 simulated minutes headless): planned for the audit milestone as a tool run (`--auto-drive` plus traffic statistics).

### M7 Audio (`Audio`)
- [ ] `AUD-001` `DynamicSoundEffectInstance` streaming harness; buffer cadence; underrun handling.
- [ ] `AUD-002` Engine synthesiser (harmonics, load, transitions) + tests on continuity/frequency.
- [ ] `AUD-003` Starter, catch, shutdown sequences.
- [ ] `AUD-004` Tyre/road noise, wind, brake, indicator tick, horn, gear clunk, collision impacts.
- [ ] `AUD-005` Mixer, volumes, settings; documented in `docs/audio-design.md`.

### M8 Environment (`Map`, `Render`)
- [x] `ENV-001` Baked terrain lighting: the macro texture carries sun shading from the terrain normal, forest canopy shade and road-verge darkening (DualTextureEffect). Roads, buildings and props use the lit BasicEffect with the same rig; sun-visibility occlusion between objects remains a polish item (PERF/UX).
- [x] `ENV-002` Planar stencil shadows for the player vehicle (`Matrix::CreateShadow` onto the ground plane under the car, stencil-masked translucent black).
- [x] `ENV-003` Town content: rows of houses and cottages along the streets, square with church, town hall and shops, prefab estate with balconies, bus shelters, benches, lamp posts, walls; generated by `BuildingGenerator`/`PropGenerator` from `objects.json`.
- [x] `ENV-004` Countryside: crop fields and meadows via the macro texture, tree avenues along the main and south roads, automatic Z 11 delineators every 50 m on rural roads. Ditches are not modelled (terrain blend only).
- [x] `ENV-005` Forest: spruce/pine/beech/oak/birch card trees sampled from forest polygons (44k trees on the sample map), gravel forest track with turning loop, timber stacks and a barrier gate.
- [x] `ENV-006` Sign set: P1, P2, P3, P4, P6, B1, B2, B20a/b, IZ4a/b, IS3a-d, IP6, IJ4c, A7a, A12a, A14, A22 faces drawn procedurally with D-DIN Bold text (`SignGenerator`, `ImageText`), mounted on posts at urban/rural heights.
- [ ] `ENV-007` Road markings: V 1a, V 2a/b, V 4, V 5 and V 6a are generated (MAP-005); pedestrian crossings (V 7) at the IP6 signs and sidewalk corners at junctions remain.
- [ ] `ENV-008` Visual pass: screenshots reviewed at each map segment; fixes recorded.

### M9 Persistence and UX
- [ ] `PER-001` Versioned save/settings JSON with tests (round trip, migration, corruption).
- [ ] `PER-002` Odometer persistence; transmission mode; selected car.
- [ ] `PER-003` Settings for mirror quality, volumes, key bindings.
- [ ] `UX-001` In-game help overlay listing controls; README controls table synchronized.

### M10 Polish and audit
- [ ] `PERF-001` Frame-time instrumentation, `--benchmark`, measurements recorded in `docs/performance.md`.
- [ ] `PERF-002` Culling/LOD/instancing tuning against measurements.
- [ ] `AUDIT-001` Asset licence audit; `assets/ASSETS.md` regenerated; manifest test.
- [ ] `AUDIT-002` Final audit checklist (section 21) executed and recorded.
- [ ] `DOC-001` README complete (status, build, controls, architecture, limitations, testing).
- [ ] `UX-006` Car body polish: nose/tail sculpting, bumper split lines, lamp housings, wheel arch lips, seam lines; compare against reference proportions in screenshots.

## 20. Acceptance criteria (product level)

The initial milestone is reached when, on a build from a clean checkout with CNA next and
Sharp Runtime next, a player can: launch; see the Czech environment; sit in the cockpit with a
functional dashboard; press `E` and hear the starter and the engine settle at idle; choose
automatic or manual; drive with accelerator/brake/clutch/gears; steer and see the steering wheel
turn; use indicators and headlights; read speed/RPM/fuel/temperature/odometer; drive through
town, countryside, forest and the minor road; meet moving traffic with Czech plates; collide
physically; watch traffic react; switch cameras; use the rear-view mirror; hear the engine react
to RPM and load; run down to reserve and see the automatic refill at 50 % of reserve.

## 21. Final audit checklist

Build from clean checkout; CNA next and Sharp Runtime next used; `CNA_CNAEXT` off; static
check clean; no renderer dependency; tests pass; sample map loads; player vehicle drives; both
cameras; steering wheel; engine start/stop; automatic and manual; clutch; indicators; lights;
dashboard; odometer; temperature; fuel; reserve lamp; refill rule; mirror; traffic; traffic
collision; static collision; plates; engine audio; provenance complete; docs current; plan.md
matches reality; clean tree; pushed.

## 22. Risks

| ID | Risk | Mitigation |
| --- | --- | --- |
| R1 | Renderer accepts only fixed vertex strides; dual-UV/instancing may differ per renderer | start-up conformance probe with fallback (per-vertex gradient lighting, non-instanced draws); test on OPENGLES3/OPENGL33/SOFTWARE |
| R2 | No custom shaders limits realism (no normal maps, no soft shadow maps) | baked lighting, environment mapping, geometry detail, texture quality, planar shadows |
| R3 | Procedural car may look less convincing than a scanned model | invest in loft quality, materials, interior detail; keep glTF path ready |
| R4 | Tyre model instability at low speed | relaxation/damping, substeps, clamps, tests |
| R5 | Traffic deadlocks at intersections | gap acceptance with timeouts, priority fallback, soak tests |
| R6 | Headless environment (llvmpipe) hides GPU-only issues | keep renderer-agnostic XNA usage; measure on real hardware when available |
| R7 | Long CNA build times slow iteration | ccache, EXCLUDE_FROM_ALL, minimal CNA options |
| R8 | Network policy blocks most asset hosts | procedural assets by default; GitHub-hosted per-item-licensed sources only |
| R9 | Renderer-specific behaviour behind the XNA API (e.g. the untextured lit `BasicEffect` path rendering black on OPENGLES3) | observed-behaviour list in `docs/framework-findings.md` section 3.4; workarounds stay inside the XNA API; screenshots after every rendering change |

## 23. Deferred features

Binary map cache and chunk streaming; side mirrors; visual damage; traffic signals runtime
logic; pedestrians; steering-wheel hardware; multiple licensed real-car models; additional maps;
normal mapping (needs custom shaders); indicator self-cancel (only with reliable steering
heuristic); weather/day-night (explicitly excluded).
