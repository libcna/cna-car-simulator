# cna-car-simulator -- engineering plan and task ledger

This file is the authoritative plan for the project. Every task has an ID, a status and
acceptance criteria. Statuses: `[ ]` open, `[~]` in progress, `[x]` done (verified, not merely
skeleton code), `[-]` deferred (with reason). Update this file in the same commit as the work.

Current scope and Phase 14 are recorded in section 27. Earlier non-goal and deferred lists are
historical records of the initial milestones, not current product exclusions.

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

- Lane graph from the map (section 11); routes planned with Dijkstra over lane links (ambient
  cars pick the next connector with a straight-through preference); spawn ring and density from
  `traffic.json`.
- Following: Intelligent Driver Model (desired speed = min(speed limit, curvature limit),
  time headway, minimum gap, comfortable deceleration) against the nearest leader in the lane
  chain (including the player's vehicle when it occupies the lane ahead).
- Intersections: priority rules from map data (main road, yield/stop, right-hand rule, left turn
  yields to oncoming; the lane graph forces right of way to be antisymmetric); gap acceptance by
  time to arrival on conflicting connectors; stop lines with a full stop; the junction box is
  kept clear (no entry while the exit or a crossing connector is blocked by a standing car);
  a car whose body lies on the path (OBB test along the path polyline) is an obstacle whatever
  the priority; deadlock breaker releases the longest-waiting car once nothing moves inside;
  a nose-to-nose stand-off is resolved by the yielding car reversing to its line. Traffic
  signals stay a deferred feature.
- Lateral: vehicles follow the lane/connector polylines kinematically (path parameter, steer
  angle from curvature); lane geometry gives yaw and height. Overtaking is not modelled: a car
  queues behind a player standing in its lane.
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
- [x] `SIM-017` Acceptance drives: 0--100 km/h (8--18 s band, measured ~14 s), braking 100--0 (36--60 m, measured 40 m), clutch stall, hill hold, straight-line stability, steering direction, odometer/fuel response; `tools/simtrace` prints traces.
- [x] `SIM-018` First vehicle definition `lipan_12.json` with documented parameter rationale.
- [x] `SIM-019` Constant-speed fuel-cycle test (`tests/Sim/FuelCycleTests.cpp`): a cruise controller holds 50 and 90 km/h in automatic mode; consumption must land in 3--7 and 4--9 L/100 km (it does without further tuning).
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
- [-] `RND-009` Renderer conformance probe at start-up. Deferred: the dual-UV terrain path was verified on the OPENGLES3 renderer (the Linux default) and no instancing is used, so there is nothing to fall back from today; the probe returns with the instanced-tree lever (PERF-002) or when a second renderer shows a difference (observed behaviours are listed in `docs/framework-findings.md` section 3.4).
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
- [x] `UI-001` Input mapper with action bindings: default keyboard bindings, action enumeration, name-based overrides from the save file (`bindings` array), `KeyFromName`/`ActionFromName`; the gamepad layer remains a deferred feature (section 23).
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
- [x] `COL-004` Vehicle-vehicle collision: `ResolveVehiclePair` (two physics bodies) and `ResolveVehicleAgainstBox` (player against a traffic car treated as a moving box with mass) are implemented and tested; the traffic integration is in `SimulatorGame::UpdateTraffic` (TRF-005), verified by driving into a queued car (the car is pushed, the AI car stops and resumes).
- [x] `COL-005` Scenario tests: wall stop from 43 km/h (no more than 6 cm penetration), offset post impact induces yaw, head-on pair separates with bounded momentum error, sample-map spawns are clear of colliders.

### M6 Traffic (`Traffic`)
- [x] `TRF-001` Route search over the lane graph (Dijkstra in `LaneGraph::FindRoute`, tested); ambient traffic picks its next link with straight-through preference (`RandomLink`).
- [x] `TRF-002` `TrafficVehicle` follows lane and connector polylines kinematically (path parameter, steer angle from curvature) with Intelligent Driver Model car following, speed limits, curve speeds and look-ahead braking; tests (free road, follower keeps distance and matches speed).
- [x] `TRF-003` Intersection behaviour from the lane graph's conflict/yield lists: priority, yield, stop (full stop at the line), right-hand rule, left turn yields to oncoming, time-gap acceptance, junction box kept clear, path-blocking check against cars inside the box, committed deadlock release, stand-off back-off (section 10); tests: minor road waits for main-road traffic, right of way antisymmetric on the sample map, 30-minute soak (TRF-008).
- [x] `TRF-004` Spawner/despawner around the player (distance ring, outside the view cone, lane spacing, `maxVehicles` from `traffic.json`), despawn beyond `despawnDistance`; test on the sample map.
- [x] `TRF-005` Player interaction: the player is projected onto the lane graph and acts as leader/obstacle, is respected in gap acceptance, and collides with traffic cars through `ResolveVehicleAgainstBox` (the AI car stops for a few seconds after a hit).
- [x] `TRF-006` `PlateGenerator` (standard `1A2 3456` series with regional weights and two-letter series, optional `EL` plates, validation, uniqueness, seeding) with tests; `PlateRenderer` draws 520 x 110 plates with the EU band, stars, `CZ` and D-DIN Bold characters; the player's plate comes from the vehicle definition.
- [-] `TRF-007` Traffic vehicle variants: eight paint colours, per-driver speed factors and unique plates are in; body variants (sedan, van) need additional vehicle definitions and are deferred (section 23).
- [x] `TRF-008` Soak test (`tests/Traffic/TrafficSoakTests.cpp`): 30 simulated minutes at 30 Hz with the player parked off the road by the square, 20 cars; no two car bodies overlap (OBB test), no car stands still for two minutes, every plate unique, more than 40 spawns. The first runs exposed mutual yields at a three-way junction and a one-frame deadlock release, both fixed; the passing run shows about 420 spawns and a handful of seconds of deadlock releases/back-offs in total.

### M7 Audio (`Audio`)
- [x] `AUD-001` `DynamicSoundEffectInstance` stereo stream (44.1 kHz, three 1024-frame blocks kept pending, underrun counter, `--no-audio`, device failure tolerated) in `Audio::VehicleAudio`.
- [x] `AUD-002` `EngineSynth`: phase-continuous harmonic bank with four-cylinder character, exhaust pulse train at the firing frequency, intake hiss, valve-train whine, per-block parameter ramps; tests for silence/fade, firing-frequency tracking, load loudness and block continuity.
- [x] `AUD-003` Starter whine while cranking (engine state `Starting`), catch clip on the transition to running, fade-out on stop/stall.
- [x] `AUD-004` Tyre noise (speed and surface), wind, two-tone horn, indicator tick/tock on lamp edges, gear clunk, collision impacts by closing speed. Brake squeal is not modelled.
- [x] `AUD-005` Software mixer with master/engine/effects levels and cockpit attenuation/low-pass blend; documented in `docs/audio-design.md`. Persisted settings arrive with M9.
- [x] `AUD-006` Engine load from delivered torque: `VehicleState::engineLoad` (combustion torque over the curve maximum) drives the synthesiser load with a small throttle share; the tyre layer already follows the ground surface class.

### M8 Environment (`Map`, `Render`)
- [x] `ENV-001` Baked terrain lighting: the macro texture carries sun shading from the terrain normal, forest canopy shade and road-verge darkening (DualTextureEffect). Roads, buildings and props use the lit BasicEffect with the same rig; sun-visibility occlusion between objects remains a polish item (PERF/UX).
- [x] `ENV-002` Planar stencil shadows for the player vehicle (`Matrix::CreateShadow` onto the ground plane under the car, stencil-masked translucent black).
- [x] `ENV-003` Town content: rows of houses and cottages along the streets, square with church, town hall and shops, prefab estate with balconies, bus shelters, benches, lamp posts, walls; generated by `BuildingGenerator`/`PropGenerator` from `objects.json`.
- [x] `ENV-004` Countryside: crop fields and meadows via the macro texture, tree avenues along the main and south roads, automatic Z 11 delineators every 50 m on rural roads. Ditches are not modelled (terrain blend only).
- [x] `ENV-005` Forest: spruce/pine/beech/oak/birch card trees sampled from forest polygons (44k trees on the sample map), gravel forest track with turning loop, timber stacks and a barrier gate.
- [x] `ENV-006` Sign set: P1, P2, P3, P4, P6, B1, B2, B20a/b, IZ4a/b, IS3a-d, IP6, IJ4c, A7a, A12a, A14, A22 faces drawn procedurally with D-DIN Bold text (`SignGenerator`, `ImageText`), mounted on posts at urban/rural heights.
- [x] `ENV-007` Road markings: V 1a, V 2a/b, V 4, V 5, V 6a (MAP-005) and V 7 zebra crossings generated across the carriageway at every IP6 sign (`RoadMeshBuilder::BuildCrossing`); junction corners are rounded by the kerb fillets of MAP-002. Verified in a screenshot of the square junction.
- [x] `ENV-008` Visual pass: screenshots reviewed for the square, the church junction, the eastern approach, the prefab estate, the forest track, the sign set, shadows, cockpit and mirror, help/debug overlays and the crossing; fixes made along the way: premultiplied font atlases, environment-map alpha sun mask, textured lit materials (R9), glass boundary classification, A-pillar width, road strip winding, sloped junction planes, terrain sink under roads, sign text orientation, binnacle hood. Final screenshots are in `docs/screenshots/`.

### M9 Persistence and UX
- [x] `PER-001` Versioned save JSON (`Core::SaveData`, schema 1) with atomic writes, corruption fallback, read-only handling of newer schemas, in-memory upgrade hook; tests for round trip, corruption, newer version, file I/O.
- [x] `PER-002` Odometer and trip, transmission mode, selected vehicle and map persist (`--save <file>`, `--no-save`; default under `$XDG_DATA_HOME/cna-car-simulator`); saved every 30 s and on exit.
- [x] `PER-003` Settings: master/engine/effects volumes (`Page Up`/`Page Down` for master), mirror on/off (`M`), HUD text on/off (`Tab`), start camera, key binding overrides as (action, key) names with validation warnings. Mirror resolution stays fixed at 768 x 200.
- [x] `UX-001` In-game help overlay (`F1`, `--help-overlay`) lists the live bindings; the README controls table is synchronised in DOC-001.

### M10 Polish and audit
- [x] `PERF-001` Frame-time instrumentation (update/draw/wall, draw calls, triangles in the debug overlay), `--benchmark` summary at exit, measurements recorded in `docs/performance.md`.
- [x] `PERF-002` Culling/LOD tuning: terrain LOD (3 levels) and distance culls cut the frame from 828k to 231k triangles and halved the software-rendered frame time (see `docs/performance.md`); batch merging and instanced trees are documented there as the next levers (section 23).
- [x] `AUDIT-001` Asset licence audit: the only external asset is the D-DIN font family (OFL 1.1, licence file and FONTLOG kept, SHA-256 in the manifest); every other texture, mesh, sound, plate and sign face is generated by project code; `scripts/check_assets.py` (CTest `asset_manifest_check`) verifies manifest fields, allowed licences, checksums and that no unlisted file sits under `assets/external/`.
- [x] `AUDIT-002` Final audit checklist executed and recorded in section 21.
- [x] `DOC-001` README complete: status, requirements, build, command line, controls (synchronised with `InputMapper` defaults), save file and binding overrides, architecture, API boundary, data-driven vehicles and maps, traffic, testing, performance, licensing and provenance, screenshots.
- [-] `UX-006` Car body polish (nose/tail sculpting, bumper split lines, lamp housings, wheel arch lips, seam lines). Deferred: the procedural hatchback reads as a car in all views and the remaining work is aesthetic; it is the first item once a licensed real-car model or more modelling time is available (section 23).

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

### Audit record (2026-09-14)

| Check | Result | Evidence |
| --- | --- | --- |
| Build from a clean checkout | pass | fresh clone of the pushed branch configured and built with the `opengles3` preset (GCC 13, Ninja); all 5 CTests passed there |
| CNA `next`, Sharp Runtime `next`, `CNA_CNAEXT=OFF` | pass | `CMakeCache.txt`: `CNA_CNAEXT:BOOL=OFF`; dependency roots point at the `next` checkouts recorded in `docs/framework-findings.md` |
| Static XNA-only check | pass | `scripts/check_xna_only.py`: 147 files scanned, no CNAEXT/renderer/platform includes or types |
| No renderer dependency | pass | the simulator never reads the renderer selection; renderer-specific behaviour is handled inside the XNA API (section 3.4 of the findings) |
| Tests | pass | 119 GoogleTest cases, `xna_only_api_check`, `asset_manifest_check`, `map_validate_lipova`, `simulator_smoke` |
| Sample map loads, vehicle drives | pass | `--spawn square/forest --auto-drive` runs, screenshots `docs/screenshots/` |
| Both cameras, steering wheel, mirror, dashboard | pass | cockpit screenshot: live cluster, wheel angle, mirror image; chase screenshot |
| Engine start/stop, automatic and manual, clutch stall | pass | unit tests (`EngineTest`, `Clutch`, `ManualTransmission`, `AutomaticTest`, `VehicleDrive`) and manual runs |
| Indicators, lights, horn | pass | `Electrics` tests; lamps in the cluster and on the body; audio tick/tock and horn layers |
| Odometer, temperature, fuel, reserve lamp, refill rule | pass | `Odometer`, `EngineThermal`, `FuelTest` (refill at 50 % of reserve to 100 %), `FuelCycle` |
| Traffic, plates, traffic collision, static collision | pass | `TrafficSystem`, `TrafficSoak`, `PlateGenerator`, `Shapes`/`CollisionWorld` tests; screenshots of a queue with plates and of a stop against a bus shelter |
| Engine audio | pass | `EngineSynth` tests (firing-frequency tracking, load loudness, continuity); stream verified with the dummy driver |
| Provenance | pass | `assets/manifest.json` + `assets/ASSETS.md`; `asset_manifest_check` |
| Docs current | pass | README, `docs/*.md` updated in the same commits as the code |
| plan.md matches reality | pass | ledger synchronised in this commit |
| Clean tree, pushed | pass | `git status` clean after the final commit; branch `claude/cna-car-simulator-project-scx0ij` pushed |

Known cosmetic limits recorded during the audit: the procedural car body (UX-006), no
overtaking in traffic, software-rendered frame times in the container (a GPU is expected to
render the frame in a few milliseconds).

## 22. Risks

| ID | Risk | Mitigation |
| --- | --- | --- |
| R1 | Renderer accepts only fixed vertex strides; dual-UV/instancing may differ per renderer | start-up conformance probe with fallback (per-vertex gradient lighting, non-instanced draws); test on OPENGLES3/OPENGL33/SOFTWARE |
| R2 | No custom shaders limits realism (no normal maps, no soft shadow maps) | baked lighting, environment mapping, geometry detail, texture quality, planar shadows |
| R3 | Procedural car may look less convincing than a scanned model | invest in loft quality, materials, interior detail; keep glTF path ready |
| R4 | Tyre model instability at low speed | relaxation/damping, substeps, clamps, tests |
| R5 | Traffic deadlocks at intersections | antisymmetric right of way in the lane graph, junction box kept clear, committed deadlock release, stand-off back-off, 30-minute soak test (TRF-008) |
| R6 | Headless environment (llvmpipe) hides GPU-only issues | keep renderer-agnostic XNA usage; measure on real hardware when available |
| R7 | Long CNA build times slow iteration | ccache, EXCLUDE_FROM_ALL, minimal CNA options |
| R8 | Network policy blocks most asset hosts | procedural assets by default; GitHub-hosted per-item-licensed sources only |
| R9 | Renderer-specific behaviour behind the XNA API (e.g. the untextured lit `BasicEffect` path rendering black on OPENGLES3) | observed-behaviour list in `docs/framework-findings.md` section 3.4; workarounds stay inside the XNA API; screenshots after every rendering change |

## 23. Deferred features

Binary map cache and chunk streaming; visual damage; traffic signals runtime logic; traffic
overtaking and lane changes; pedestrians; multiple licensed real-car models; car body polish
(UX-006); additional maps; batch merging and instanced trees (PERF-002 levers); renderer
conformance probe (RND-009); normal mapping (needs custom shaders).

Since delivered (after Phase 13): side mirrors, gamepad and steering-wheel input, indicator
self-cancel, traffic buses and lorries (the heavy part of TRF-007), a limited-slip
differential, rain on the windscreen with wipers, tyre spray and puddles.

Traffic junction logic, reworked with the buses and lorries: the 30-minute soak passed on its
seed (21) by luck -- seeds 21-30 gave 56 overlap events on the old code, clean on 4 of 10. Fixed:
a deadlock release could put a car in front of traffic too fast to stop; a clearing release
stayed committed into the next junction; a car committed in a queue blocked every release; a
waiting car stopped just over the line counted as entering; left turns on green crossed the
oncoming stream without giving way, and on green everyone waited for cross traffic standing at
red; a long vehicle never "stopped at the line" of a stop sign. Cars now claim a junction
(`claimed`) once inside their stopping distance, and nobody enters while a conflicting car is
still crossing. `CARSIM_SOAK_SEED` runs the soak on another seed. With the later rounds
(overtaking, bus stops, people, and the permissive-turn and release fixes) all hundred seeds
21-120 run the 30 minutes without an overlap or a stuck car, and a ten-minute town soak with
people crossing checks that no car ever drives into a person on the road.

Day and night, weather, traffic signals and a larger world were deferred here until Phase 12
(section 25) took them on at the owner's request.

---

## 24. Phase 11 -- Realism & Production Quality (`RQ`)

Started 2026-09-14 on top of the M10 milestone (`c337c89`). Purpose: keep the simulator's
technical foundation and make it look and feel like a small finished driving simulator instead
of a procedural technical demonstration. No new gameplay systems (section 2 and the M10 deferred
list stay as they are); every task below must make the existing driving experience visibly more
believable, more polished or faster. The API boundary (section 3) is unchanged and the XNA-only
static check remains mandatory.

### 24.1 Audit of the M10 state (2026-09-14)

Fresh captures from the M10 build are kept in `docs/screenshots/m10-baseline/` (Xvfb + Mesa
llvmpipe, OPENGLES3 renderer; software rendering, so frame times there are not GPU numbers).
Build and tests re-run before the audit: 119 GoogleTest cases and 5 CTests pass. Findings, in
priority order:

| Area | Finding |
| --- | --- |
| Player car | The lofted hatchback reads as a box: flat vertical nose and tail caps, no plan-view rounding, sharp roof and sill edges, a continuous black band along the whole lower body, lamp units are floating rotated boxes, the grille is a box, mirrors and handles are boxes, no panel gaps, glass is recoloured loft quads without a recess. |
| Wheels | Tyre = torus + cylinder, rim = flat disc with five thin box spokes (reads as a white disc with lines from the side); no brake disc, no rim lip, no tread. Contact with the road is correct (radius and pivot come from the simulation). |
| Cockpit | Dashboard is a slab, the binnacle a box; the A-pillars appear as huge grey wedges because the interior shell copies the loft's painted band from the belt line to the roof corner; the left mirror housing is a floating black box; no vents, no console details, no gear lever, seats are boxes. Steering wheel and cluster work. |
| Dashboard | Functional and readable; typography, needle shape, lamp icons and the digital area are plain. |
| Materials | Every part shares one textured lit `BasicEffect` look; paint has an environment map; glass is a flat tint; no texture on any car surface except plate and cluster. |
| Traffic | Same body for every car; only paint differs. |
| Buildings | Boxes with window quads glued on flat plaster; no reveals, gutters, eaves fascia, plots, fences or gardens; houses stand loose on a green plane. |
| Roads | Widths and markings are right; sidewalks are a high-contrast checkerboard; the road edge meets the terrain with a hard line; the asphalt is uniform. |
| Vegetation | Card trees with cartoon blob crowns; forests show regular rows of identical trunks; no bushes, no roadside grass detail; forest edge abrupt. |
| Lighting | Flat: strong ambient, weak sun, saturated greens; sky is a plain gradient with barely visible clouds; no ground shadows from buildings or trees. |
| Cameras | Chase camera framing is fine; no look-ahead in turns; reversing keeps the camera in front of the direction of travel. Cockpit eye position and FOV are plausible. |
| Audio | Engine synthesis is continuous and load-driven but uses a single harmonic bank; no surface-dependent rolling noise; no brake or wind detail. |
| Performance | `--benchmark` on llvmpipe: update 0.27 ms, draw submission 17 ms, 238 draw calls, 140k triangles (east spawn, traffic). No per-pass timings. |

Asset approach decision for the hero car: option A (dramatically improve the project-owned
procedural vehicle). Blender is not available in the development environment, no legally clean
real-car model is reachable (see `docs/research/assets.md`), and a procedural body keeps the
traffic variants and the licence audit trivial. The Lipan identity is kept and developed.

### 24.2 Task ledger

Statuses as in section 19. Acceptance criteria are what a reviewer checks; "screenshot" means a
capture under `docs/screenshots/` reviewed against the baseline.

#### Hero car exterior
- [x] `RQ-001` Baseline captures and audit table (this section); `docs/screenshots/m10-baseline/`.
- [x] `RQ-010` New Lipan body surface: dense station loft (<= 4 cm) with plan-view rounding of nose and tail, sculpted hood/cowl/roof/tailgate profile, fender flares over the arches, tucked sills, smooth tumblehome, slim A/B/C pillars (glass classified against pillar bands, not ring segments), recessed glass, separate bumper skins with air dam and fog-lamp recesses, grille recess, headlamp and tail-lamp housings, shaped mirrors on stalks, door handles, wipers, exhaust, antenna. Acceptance: front 3/4, rear 3/4 and side close-ups show no flat caps or box lamps; the cabin from inside has slim pillars; wheel transform tests unchanged; body under 60k triangles.
- [x] `RQ-011` Body detail texture: UV-mapped loft with a generated paint texture (door and hood shut lines, tailgate seam, fuel flap, sill and arch ambient darkening) through `EnvironmentMapEffect`'s texture. Acceptance: shut lines visible in side close-up; paint colour still authoritative from the definition.
- [x] `RQ-012` Wheels and tyres: revolved tyre profile (tread, shoulder, sidewall bulge, bead) with a tread/sidewall texture, revolved rim (lip, dish, well) with five twin spokes that have depth, hub cap, brake disc and caliper behind the spokes. Spin/steer/suspension unchanged (existing tests); a new test checks the tyre mesh touches y = 0 within 1 cm at the definition radius.
- [x] `RQ-013` Vehicle lights: headlamp units (reflector texture + clear lens), tail-lamp clusters (red/amber/white segments in one housing), side repeaters; emissive states from `VehicleState` only; daytime lens look when off. Acceptance: lights screenshot with lights off/on/brake/indicator.
- [x] `RQ-014` Material audit: distinct looks for paint, glass (tint + frit band texture), rubber, black plastic, chrome, interior fabric and plastics (grain textures), lamp lenses, plate; documented in `docs/materials.md`. All through stock effects.

#### Cockpit and dashboard
- [x] `RQ-020` Cockpit rebuild: shaped dashboard (curved top, binnacle cowl, centre stack with vents and controls, glovebox), steering column, gear lever that follows the transmission state, handbrake, shaped front seats with head restraints, door cards with armrests, slim A-pillars with trim, headliner, sun visors, mirror housing, windshield frit band. Acceptance: cockpit screenshot without grey wedges, steering wheel still synchronised (test), no geometry closer than the near plane.
- [x] `RQ-021` Instrument cluster presentation: redesigned faces (typography, tick hierarchy, red zone), needle with hub and shadow, lamp icons redrawn, backlit look with ignition, digital display area (odometer, trip, gear, consumption, clock-free). Simulation values remain authoritative (`LampLit` test kept). Acceptance: `--screenshot-cluster` review.

#### Traffic and vehicle variety
- [x] `RQ-030` Traffic body variants: generator presets for hatchback, sedan, estate, small SUV and van (dimensions, greenhouse, overhangs, roof line, ride height) selected per traffic car with paint, wheel style and plate; traffic cars share materials. Acceptance: traffic screenshot with at least three distinct silhouettes; soak test unchanged.
- [x] `RQ-031` Vehicle LOD: traffic beyond a near radius drops interior, glass, shadow and small parts; beyond a far radius uses a reduced body. Draw calls per traffic car reported in the debug overlay.
- [x] `RQ-032` Traffic presentation polish: lane centring and steering smoothness checked while driving, spawning outside the view, wheel spin matches speed, brake lights and indicators verified. Test `NewCarsAppearOutsideThePlayersViewConeAndWheelsSpinWithSpeed` (no car appears within 230 m inside the 55 degree cone ahead; wheel spin integrates the travelled distance at the 0.31 m reference radius); brake lights and indicators checked in the traffic captures.

#### Environment
- [x] `RQ-040` Fixed daytime lighting rebalance: stronger sun, cooler and weaker ambient, sky and ground fill tuned, fog haze colour matched to the sky, sky dome with proper horizon glow and readable clouds. Acceptance: before/after pair for town and countryside; cockpit not crushed; paint reads.
- [x] `RQ-041` Static ground shadows baked into the terrain macro texture (buildings, trees, walls projected along the sun) and contact shadows under cars; vehicle planar shadow softened with a second offset pass; no shadow acne. Acceptance: shadows visible beside buildings and under avenues; frame cost unchanged (baked). Done: vehicle sun shadow (stencil-free convex hull with penumbra rim, draped on the ground) and contact shadow; `GroundShadowBaker` bakes building sweeps and tree crown discs into the macro and the road vertex colours.
- [x] `RQ-050` Roads: reworked asphalt (wear tracks, patches, edge weathering), quieter sidewalk paving, kerb profile with gutter, grass verge strip blending road and terrain outside town, gravel shoulder texture, intersection surface continuity, marking wear. Acceptance: road no longer reads as a clean strip on a plane; lane widths unchanged (map tests).
- [x] `RQ-051` Czech road details review against `docs/research/czech-roads.md`: sign faces 0.7 m (circles, P 2/P 3 diamonds, P 6 octagon), 0.9 m (warning and P 4 triangles), 0.5 m (IP 6, IJ 4c), 1.0 x 0.5 m (IZ 4a/b), 1.6 x 0.4 m (IS 3); lower edge 1.5 m rural and 2.2 m in built-up areas on 64 mm posts; Z 11 delineators every 50 m on rural class I-III roads at 0.35 m outside the shoulder, 1.05 m high with the black band at 0.70-0.95 m, orange reflector towards the driver on the right and white on the left; V 7 crossing 0.5 m bars and gaps, 4 m long; V 5 stop line 0.5 m wide 1 m before the junction patch; V 6a give-way triangles 0.6 m. All within the TP 65 / TP 133 basic sizes; no correction was needed.
- [x] `RQ-060` Building kit: window reveals with frames and sills as geometry, lintels, cornice and eaves fascia, gutters and downpipes, chimneys with caps, entrance steps, plinth, roof variants (gable, hipped, half-hipped) with ridge tiles, dormers on some houses, facade texture variation; block houses with balcony railings and entrance canopies. Acceptance: town screenshots without floating windows or bare boxes.
- [x] `RQ-061` Plots and street furniture: fences (wood, wire, wall) and hedges around house plots with gates and driveways generated from the placed buildings, garden sheds, utility poles along village roads, bus shelter and bench polish. Acceptance: houses no longer stand loose on the meadow. Done: street-side picket/wire/hedge lines with a gate gap, side fences on cottages, sheds behind every second house, utility poles on class III/local/residential roads; open: driveways, shelter/bench polish.
- [x] `RQ-070` Vegetation: new species card textures (lit crowns, several variants per species), near-tree trunk with branches, bushes along roads and forest edges, roadside grass tufts within 60 m, forest understory darkening and edge blending, jittered placement with clumping. Acceptance: forest screenshot without visible rows; town avenue reads as trees. Done: darker, finer-grained crowns with an underside shade gradient, a bush species along rural verges and forest edges; open: grass tufts, branch geometry.
- [x] `RQ-071` Terrain surface: less saturated multi-scale grass, crop textures with rows, dirt near roads, meadow variation; macro tint tuned with the lighting rebalance.

#### Cameras, mirror, audio, driving
- [x] `RQ-080` Chase camera: spring-damped follow with speed-dependent distance, look-ahead in turns, correct reversing behaviour, low-speed stability, terrain clipping avoidance. Cockpit camera: eye position and FOV verified, tiny motion cues, no jitter. Acceptance: description in `docs/cameras.md` and a scripted drive without visible jumps. Defect found in the screenshot loop and fixed: the orbit basis was the mirror image of -forward, so the camera sat in front of an east-bound car and beside a north-west-bound one (present since M10); regression test `SitsBehindTheCarAndAimsAheadForEveryHeading`.
- [x] `RQ-081` Mirror: framing and FOV checked, traffic visible, optional half-rate update setting, cost measured and recorded (llvmpipe: 62.8 ms per frame at every frame, 31.8 ms at every second frame).
- [x] `RQ-090` Audio polish: layered engine (intake/exhaust/mechanical crossfades by load and rpm, overrun burble, gear-change dip), surface-dependent rolling noise, brake and wind layers; tests for continuity and level ordering; `docs/audio-design.md` updated.
- [x] `RQ-100` Driving feel audit: scripted drives at parking, 50 and 90 km/h, launches, braking, reversing, slopes; defects fixed with regression tests.

#### Performance and validation
- [x] `RQ-120` Instrumentation: per-pass CPU timings (cluster, mirror, world, traffic, vehicle, HUD), visible/culled counts per class, traffic count, draw calls, triangles in the debug overlay and in the `--benchmark` summary (also written as JSON with `--benchmark-json`).
- [x] `RQ-121` LOD and culling: distance culling for props and buildings with far LOD, tree far LOD, vehicle LOD (RQ-031); measured before/after in `docs/performance.md`.
- [x] `RQ-130` Renderer conformance: build and run the same code on the renderers available in the environment (OPENGLES3, OPENGL33, SOFTWARE where it links); results, screenshots and differences recorded in `docs/renderer-conformance.md`. No renderer-specific project code.
- [x] `RQ-131` `docs/real-hardware-validation.md`: reproducible procedure for a real PC (build, launch, views, controls, overlay, capture, metrics to report, checklist).
- [x] `RQ-140` Screenshot loop: curated final set in `docs/screenshots/` (hero exterior, cockpit, dashboard, traffic, town, countryside, forest, intersection), README updated. Set: `hero.jpg`, `cockpit.jpg`, `cluster.png`, `town.jpg`, `traffic.jpg`, `countryside.jpg`, `forest.jpg`, `intersection.jpg` (JPEG quality 88 for the scene captures, PNG for the cluster texture); the M10 set stays in `m10-baseline/`, renderer grids in `renderers/`. The loop found and fixed the chase camera orbit defect (RQ-080).
- [x] `RQ-150` Final audit: fresh clone build, all tests, static and asset checks, plan/README synchronised, final SHA recorded in section 24.4.

### 24.3 Principles for this phase

1. Preserve stable systems (physics, traffic, map, collision, save, plates); refactor only for a
   concrete defect, performance problem or realism limit, and add regression coverage.
2. Every visual task: capture before, implement, capture after, compare, iterate.
3. Prefer fewer high-quality improvements to many mediocre ones; the hero car and cockpit come
   first.
4. Anything that needs CNAEXT, renderer internals or custom shaders is rejected and solved with
   geometry, textures, baked lighting and stock effects.
5. No new external assets unless their licence is verifiable and recorded; procedural first.

### 24.4 Phase record

Filled in as tasks complete (commit per logical unit; final SHA at the end of the phase).

- `b1c8167` Hero car rebuilt: `ProceduralCar` is a dense fixed-topology loft driven by smooth
  longitudinal curves (`CarBody.hpp`, `ProceduralCar.cpp`), with rounded-box sweeps for nose
  and tail, sculpted face grids (grille, intake, plate recesses), decal lamp units cut from the
  skin in UV space, fender flares, slim pillars, recessed glass, mirrors, handles, wipers,
  exhaust, antenna, badges, fog lamps; revolved tyres and rims with twin spokes, brake discs and
  calipers (`CarWheels.cpp`); UV-mapped paint detail texture with shut lines and sill/arch
  darkening and a premultiplied glass tint with frit bands (`CarTextures.cpp`); glass reflects
  the sky from outside and is nearly clear from inside. Tests: `ProceduralCarTests`
  (roles, chassis box, tyre contact at the definition radius, every style, UV layout).
- Cockpit rebuilt (`ProceduralCockpit.cpp`): two-tone dashboard loft with binnacle visor,
  centre stack (display, vents, knobs), glovebox seam, steering wheel with hub badge, column
  and stalks, gear lever animated from the transmission state (H pattern / P-R-N-D), handbrake,
  bolstered seats, door cards with armrests, inner shell (pillars dark at the base, light
  headliner), parcel shelf, mirror, visors. Cabin parts are also drawn from outside so cars are
  not hollow. `--eye dx dy dz yaw pitch` moves the cockpit camera for inspection captures.
- Traffic variety (`CarStyle` presets in `Sim/CarStyle.cpp`, `TrafficRenderer`): hatchback,
  sedan, estate, SUV and van in two size variants each, picked at spawn with matching
  collision dimensions and masses; ten paint colours (vans mostly white/silver); distance LODs
  (full / no small parts / reduced without cabin, glass and plate) and a one-part cabin block
  for traffic models. `--lockstep` gives one simulation step per drawn frame so captures on
  slow renderers are deterministic; `--traffic-warmup <s>` pre-runs the traffic for captures.
- `563ad0d` Lighting rebalance (`LightingRig.hpp`): sun 0.98/0.93/0.84, ambient 0.21/0.23/0.28,
  sky fill 0.15/0.18/0.24, ground bounce 0.10/0.09/0.07, so a sunlit horizontal surface sits
  near 1.0 instead of clipping; haze 300-2600 m; terrain macro tints desaturated; grass and
  cloud textures reworked; lamp glow sprites (`LampGlow` anchors, additive billboards) for the
  player and near traffic; `--lights` capture option.
- Vehicle shadows rebuilt without the stencil (`ShadowGeometry.cpp`, `VehicleRenderer::DrawShadow`):
  the old per-part planar shadow relied on `Equal 0` stencil tests against a per-frame stencil
  clear, which stopped taking effect after the second frame on the EasyGL path, so cars had no
  shadow at all in practice (found by frame-1/2/3 captures). Now each model keeps the extreme
  vertices of its body and wheels (Fibonacci-sphere support points); every frame they are
  projected along the sun onto the ground, their convex hull is drawn once as a fan with a
  12 cm penumbra rim, plus a soft contact shadow under the footprint. Vertices are draped on
  the sampled ground (roads, kerbs, terrain) through `GroundQuery`; drawn through a refilled
  vertex/index buffer (the user-primitive path was unreliable after the town world pass).
  Tests: `ShadowGeometryTests` (hull, support points, projection, rim normals, Lipan silhouette
  area/containment/offset under the fixed sun).
- Cockpit seating reference fixed (`VehicleDefinition` visual defaults, `lipan_12.json`,
  `ProceduralCockpit.cpp`): the driver's eye sat level with the windshield header (z 0.12 with
  the header at -0.02), so the A-pillar top and sun visors were at the camera and the interior
  mirror hung above the glass. The eye now sits at the B-pillar (z 0.38, 0.40 m behind the
  header), the wheel 0.56 m ahead, the mirror hangs from the glass on an angled stem, the dash
  is 5 cm deeper with a longer column shroud, seats and armrests follow the eye. Two defects
  this exposed: the seat backrests (front, rear bench and the traffic cabin block) leaned
  forward because of a rotation sign, and the dashboard loft was wound inside out (its
  orientation check passed for both windings), which lit the occupant-facing panel from the
  sun as a sawtooth of bright triangles. `MeshData::SignedVolume/OrientOutward` now orient
  closed lofts. Seat fabric darkened. Tests: `CockpitPlacementMatchesTheSeatingReference`,
  `SeatBackrestsLeanRearwardBehindTheEye`, `OrientOutwardFixesAnInsideOutLoft`.
- Materials and cluster: `docs/materials.md` records every `CarMaterial` look (effect, texture,
  diffuse/specular/emissive/env amount); door mirror glass gets its own dark reflective slot
  (`MirrorGlass`) instead of white chrome; the reversing segment of the tail cluster is a
  narrow inner strip and the tail lens is a deeper red; the instrument cluster gains brushed
  bezel rings, gradient faces, needle drop shadows, chrome hubs and a recessed LCD panel
  (`InstrumentCluster.cpp`). Lights verified in captures (headlamps, tails, indicators, glows).
- Roads and ground (`RoadMeshBuilder.cpp`, `WorldRenderer.cpp`, `GroundShadowBaker.cpp`):
  road strips now carry their lighting in vertex colours (rig irradiance per normal, drawn
  unlit with fog) so the paved surface can hold wheel-track wear (polished tracks lighter,
  lane centre and outer 0.5 m darker, slow tone variation along the road), packed gravel
  shoulders, quieter paving and worn marking paint; rows are at most 2.5 m apart and the
  paved surface has 0.25 m columns. Rural pieces get a 2 m grass verge that drapes from the
  shoulder edge to the sampled terrain and blends into the exact macro colour, so roads no
  longer float 12 cm above the ground. Building and tree shadows are baked into a 2048^2
  ground shadow map (swept footprints at 0.45, crown discs at 0.52, one blur pass) that
  multiplies the terrain macro and the road vertex colours. Calibration: CNA's
  DualTextureEffect does not double detail x macro, so the macro now carries the full
  light (sunlit meadow ~0.33, asphalt ~0.29) and the grass texture, tints and tile size
  (7 m) were retuned. Tests: `RoadMeshBuilderTests` (wear band, verge drape), `GroundShadowTests`
  (offset, sample map statistics, per-building shade side), `LightingRig::Irradiance`.
- Building kit and plots (`BuildingGenerator.cpp`, `PropGenerator.cpp`, `ObjectPlacement.cpp`):
  windows get geometric frames standing proud of the wall with a dark reveal line, every
  eave a fascia board, gutter and downpipes, ridges get ridge tiles, chimneys caps and pots,
  doors a step and canopy, town houses a cornice and string course, some two-storey gabled
  houses a dormer, every fifth house a hipped roof; prefab blocks get concrete balcony slabs
  with handrails and an entrance canopy. `ObjectPlacement::PlacePlots` generates street-side
  picket, wire or hedge lines with a gate gap for houses and cottages (side fences on
  cottages), a shed behind every second one, and `PlaceUtilityPoles` puts wooden poles with
  crossarms along class III, local and residential roads; all placements are checked against
  buildings and roads. Detail batches (frames, gutters, reveals) cull beyond 420 m. Test:
  `PlotsAndUtilityPolesAreGeneratedClearOfBuildingsAndRoads`.
- Vegetation (`VegetationGenerator.cpp`, `ObjectPlacement::PlaceBushes`): crown cards use
  darker, less saturated leaf palettes with more and smaller blobs and a vertical shade
  gradient (lit from above, dark underside); a `Bush` species (2.2 m, no trunk) is scattered
  along rural road verges (45 % of 9 m steps, 3-5.5 m off the road) and just outside forest
  polygon edges, clear of buildings, roads and existing trees; bushes cast baked shadow discs
  like trees. Test: `BushesLineRuralVergesAndForestEdges`.
- Cameras, mirror rate, instrumentation, audio layers, driving feel (`Camera.cpp`,
  `SimulatorGame.cpp`, `AudioLayers.cpp`, `VehicleAudio.cpp`): the chase camera follows with a
  frame-rate independent exponential (9/s), pulls back and rises with speed, follows the body
  yaw at 2.5-5.5/s so parking and reversing do not swing the view, slides its aim point up to
  1.6 m into a bend from the filtered yaw rate, and is clamped 0.7 m above the sampled ground
  (same query as the physics) so embankments never swallow it; jumps over 20 m snap. The
  cockpit eye/FOV are documented in `docs/cameras.md`. The mirror target can be redrawn every
  n frames (`mirrorUpdateEvery`, `--mirror-every`), keeping the previous image in between.
  Per-pass CPU timings (cluster, mirror, sky, world, traffic, vehicle, HUD), visible batch
  counts per class and traffic LOD counts are shown in the debug overlay and in the
  `--benchmark` summary, which `--benchmark-json <file>` also writes as JSON;
  `docs/performance.md` records the Phase 11 table and the LOD/culling levers. Audio gains a
  gear-change dip, an overrun burble gate, surface-dependent rolling noise from the wheel
  contact surfaces and a brake hiss layer (`docs/audio-design.md`). Tests: `CameraTests`
  (frame-rate independence, speed pull, ground clearance, reversing, look-ahead),
  `AudioLayersTests`, `ParsesBenchmarkJsonAndMirrorRate`, and the driving-feel scripts in
  `VehicleDriveTests`: `FullLockAtParkingSpeedTurnsInAPlausibleCircle`,
  `ReverseGearDrivesBackwardsAndSwingsTheNoseTheOtherWay`, `HoldsFiftyOnTheFlatWithPartThrottle`,
  `ClimbsAnEightPercentGradeWithoutLosingMuchSpeed`.
- Renderer conformance and validation procedure (`docs/renderer-conformance.md`,
  `docs/real-hardware-validation.md`): the `opengles3`, `opengl33` and `software` presets were
  built from the same source (no renderer-specific code; `vulkan` has no ICD in the container)
  and the same lockstep town frame captured on each: identical draw calls and triangles
  (606 / 575k exterior, 869 / 832k cockpit), pixel differences of 0.4-6.5 levels mean and
  under 3.5 % of pixels over 32 levels, all explained by edge rasterisation and llvmpipe's
  anisotropic filtering on the ES3 path; the cluster target is bit-identical between the GL
  renderers. Comparison grids in `docs/screenshots/renderers/`. The SOFTWARE renderer runs
  the scene at 1.8 s per frame, fifteen times slower than llvmpipe. The validation page gives
  the build, launch, checklist, capture set, benchmark set and reporting list for a real PC.
  Traffic presentation (RQ-032) got a spawn-visibility and wheel-spin test; the Czech road
  detail review (RQ-051) found all sizes within the basic TP 65 / TP 133 values.
- Chase camera defect (found while curating the final screenshots): `ChaseCamera` built its
  "behind" vector as (-sin yaw, 0, cos yaw), the mirror image of -forward for any heading off
  the north-south axis, so every exterior capture since M10 showed the car from the front
  (square spawn, east-bound) or from the left-rear quarter (forest spawn); the look-ahead
  side was mirrored the same way. Fixed in `Camera.cpp` (behind = (sin, 0, cos), right =
  (cos, 0, -sin)); test `SitsBehindTheCarAndAimsAheadForEveryHeading` checks seven headings.
- Curated screenshots (RQ-140): eight captures from the OPENGLES3 build with the corrected
  chase camera (hero three-quarter rear at the square, cockpit and cluster at 35 km/h with a
  car ahead and traffic in the mirror, town street drive, oncoming traffic, avenue through
  the fields, forest edge, the square junction with its crossing and a traffic car), README
  tables and status text updated, superseded `town-street/cockpit/forest-road` removed.
  A `kostel` player spawn (90 m before the church junction, east-bound) was added to
  `traffic.json` for junction captures; `map-validate` reports 0 warnings.
- Headlamps and the `--lights` capture flag: the unlit headlamp lens was nearly white
  (diffuse 0.85/0.88/0.92), so a lit lamp was indistinguishable from an unlit one in daylight;
  the lens now reads as glass over a grey reflector when off (0.52/0.54/0.58) and brightens to
  0.92/0.93/0.95 with emissive 0.78 when the low beam is on (`docs/materials.md` updated).
  While checking this, `--lights` turned out to be a no-op on its own: the electrical system
  gates every lamp except the hazards on the ignition, and without `--auto-drive` the engine
  stayed off, so the two captures were byte-identical. `--lights` now starts the engine itself
  when no scripted drive does and toggles the headlights once the ignition is live; the lit and
  unlit captures differ clearly (lens, glow, plate lamp). The curated forest picture was
  replaced by a view deeper in the spruce stands and a headlights picture was added to the
  README. The yellow chevron visible in the sky of the forest captures was tracked down to the
  HUD indicator telltale drawn at the top centre, not a world artifact.
- Hero body close-up review (closes `RQ-010`): front, rear and side close-ups at 4 m show no
  flat caps or box lamps, the cabin has slim pillars from inside and the body stays under the
  60k triangle budget (test). Two defects the review found were fixed. The fog lamps were
  placed at the depth of the centre-line nose tip, but the nose sweeps inwards towards the
  corners, so the outboard lamps hung beside the bumper over the road; they are now projected
  onto the skin with `FrontFacePoint`, which also tilts each ring with the surface (test
  `FogLampsFollowTheNoseInsteadOfHangingBesideIt`). And a skin quad became a recessed lamp
  housing when its centre fell inside the lens polygon, so housing quads stuck out past the
  headlamp and tail lamp lenses as black notches while paint quads intruded under the lens edge
  as red slivers; a quad is now cut only when all four of its corners are inside the polygon and
  the lens decals sit 6 mm proud instead of 3 mm.
- Interior mirror height: `mirrorCenter` sat at 1.17 m, three centimetres above the driver's
  eye, so in the cockpit view the mirror housing hung at the horizon and covered the right-hand
  third of the windscreen. It now hangs at 1.28 m, 0.12 m ahead of the origin, under the
  windscreen header; the cockpit placement test gained an assertion that the housing's lower
  edge stays at least 5 cm above the eye. The curated screenshots were re-captured with the
  final build (fog lamps, lens housings, mirror).

#### Phase 11 final audit (RQ-150)

Verified commit `a1a7efd83a83c17347ef5c6b98082fcaf7524014` by cloning the branch fresh from
`origin` into an empty directory and building it against the dependency checkouts
(`-DCARSIM_CNA_ROOT` and `-DCARSIM_SHARP_RUNTIME_ROOT`; CNA `next`, Sharp Runtime `next`,
EasyGL and MetaGL beside them), Release, Ninja, `CNA_GRAPHICS_RENDERER=OPENGLES3`:

| Step | Result |
| --- | --- |
| Configure and build every target (simulator, tests, four tools) | no errors, no warnings in project code |
| `ctest` | 5/5 registrations pass in 27 s (152 unit and scenario tests, the headless smoke run, both static checks, the content check) |
| `scripts/check_xna_only.py` | OK, 166 files scanned against 544 XNA 4.0 types |
| `scripts/check_assets.py` | OK, 5 files listed, 1 asset |
| `carsim-mapvalidate content/maps/lipova` | OK, 0 warnings |
| Headless smoke capture (60 frames, scripted drive) | frame written, no errors |

`README.md` and this ledger agree: every row of section 24 is `[x]` and the README claims no
feature the ledger does not mark as done.

### 24.5 Follow-up work after the Phase 11 audit

Small, self-contained improvements found while reviewing the finished build. Each one is
committed with its own regression test and screenshot check.

- [x] `RQ-160` Town square: the centre of Lipova was a lawn. A new `square` terrain region is
  paved with generated granite setts (`Textures::Cobbles`, running bond, domed tops, dark
  joints) drawn as a ground mesh draped on the terrain and cut around the roads
  (`WorldRenderer::BuildPavedAreas`); the ground there drives and sounds like cobbles
  (`SurfaceType::Cobbles`). The sample map gains a 77 x 75 m namesti with the church standing
  on it, eleven two- and three-storey town houses and shops lining the west, east and north
  sides, a row of lime trees, benches, bins and lamps. Test:
  `TheSquareIsPavedAndLinedWithTownHouses`; `docs/map-format.md` documents the region type.
- [x] `RQ-161` Parked cars: `objects.vehicles[]` places a static car of any body class with a
  heading and a seed (`Map::PlacedVehicle`). They are drawn by `TrafficRenderer::DrawParked`
  with the same models, paint palette, plates, distance LODs and ground shadows as moving
  traffic but standing still with the engine off, and they are solid: `CollisionWorld::Build`
  adds a box the size of the body class. Fifteen cars are parked nose-in around the Lipova
  square. Plates come from a `PlateGenerator` seeded per map, so captures stay comparable.
  Tests: `ParkedCarsStandOnTheSquareClearOfBuildingsAndRoads` (on the paving, off the
  carriageway, clear of every footprint, one collider each); `carsim-mapvalidate` reports the
  parked cars; `docs/map-format.md` documents the array.
- [x] `RQ-162` Memorial column on the square: a new `memorial` prop (two stone steps, a
  pedestal, a tapered shaft with a capital and an iron cross, no figure) stands between the
  church and the middle of the namesti and is solid. Covered by the square test.
- [x] `RQ-163` Street parking: `ObjectPlacement::PlaceStreetParking` parks a car at the kerb of
  urban local and residential streets every 18 m with a 55 % chance (about one car per 33 m,
  alternating sides, nose with the traffic), skipping junction approaches, building footprints
  and anything whose centre is less than 0.3 m outside the paved carriageway, so the traffic
  never meets them. The Lipova map gains 76 street cars on top of the 15 on the square; the
  town benchmark rises by 3.5 ms (llvmpipe). Test: the parked-car test now checks the authored
  cars on the square and every generated car for clearance of the carriageway and junctions.
- [x] `RQ-164` Prefab blocks: the gable ends were blank slabs and the loggia parapets were dark
  brown (the trim colour). Each floor now gets a pair of small windows on both end walls, the
  loggias and the lift housing are concrete, and a second row of loggias sits on the rear
  facade offset by one bay so the two long sides differ.
- [x] `RQ-170` Follow-up audit: commit `124cecdd6cd2261b7db9163ffab1370a9c7cfd74` cloned fresh
  from `origin` and built against the dependency checkouts (Release, Ninja, OPENGLES3): every
  target builds, `ctest` passes all five registrations in 29 s (156 unit and scenario tests,
  the headless smoke run, both static checks, the content check), `check_xna_only.py` and
  `check_assets.py` are clean and `carsim-mapvalidate` reports no warnings.
- [x] `RQ-165` Forest track: the end loop of the forest track ran 100-180 m beyond the north
  edge of the wood, so the "forest" road finished in open meadow. The big forest polygon now
  reaches z = -2840 and wraps the loop (52.3k trees instead of 46.5k, forest benchmark
  unchanged at 55 ms).
- [x] `RQ-166` Eastern junction: the node was named "U kostela" (at the church) although the
  church stands on the square 400 m away. A wayside chapel with its bell tower now stands at
  the junction and the node is named "U kaple".
- [x] `RQ-167` Garden trees: the fenced plots behind village and town houses were empty lawns.
  `ObjectPlacement::PlaceGardenTrees` scatters one to three trees per house and cottage on the
  far side of the building from its street, clear of buildings, roads, the square and other
  trees (662 trees on the sample map). Benchmarks unchanged within noise.
- [x] `RQ-168` Horizon apron: the terrain grid stops 2 km from the centre, and from the hills the
  edge read as the world ending in mid-air. `WorldRenderer::BuildTerrain` now also builds a flat
  skirt that carries each edge vertex height and the clamped macro colour 8 km outwards, so the
  ground runs into the fog (one extra draw call, about 700 triangles).
- [x] `RQ-169` Help overlay wording: the gear row read "1 - 6 / 1st gear" and the shift rows were
  cut off as "Left Shift / Right Sh...". The row now reads "Select a gear (manual)" and the
  modifier names are shortened to "L Shift / R Shift" instead of being truncated.
- [x] `RQ-171` Filling station and paved yards: a `yard` terrain region paves with concrete slabs
  (`Sim::SurfaceType::Concrete`), and paved areas with a four-corner outline are now filled with
  a bilinear grid over the outline itself, so a rotated forecourt has straight edges instead of
  a stair-stepped boundary. New props `fuel_canopy` (deck on four solid columns) and `fuel_pump`
  (island, body, displays, hose stacks) make a station on the eastern approach of Lipova, with a
  shop building, a lamp, a bin and a car on the forecourt.
- [x] `RQ-172` Meadow trees: the open country between the villages was a bare lawn.
  `ObjectPlacement::PlaceMeadowTrees` scatters a solitary tree or a clump of three per 130 m
  cell with a 42 % chance, only inside meadow regions and clear of roads, buildings and other
  trees (570 trees; the fields benchmark moves from 25.4 to 26.7 ms).
- [x] `RQ-173` Content-pass audit: commit `206066fbd5fde40aa701b63457584fc363943db0` cloned
  fresh from `origin` and built against the dependency checkouts (Release, Ninja, OPENGLES3):
  every target builds, `ctest` passes all five registrations in 32 s (159 unit and scenario
  tests), both static checks are clean and `carsim-mapvalidate` reports no warnings.
- [x] `RQ-174` Paint palette: ten colours became sixteen (graphite, champagne, petrol, orange,
  midnight grey, burgundy on top of the common ones) and the traffic draws from a weighted table
  so white, silver, grey and black still take about two thirds of the cars. Parked cars use the
  whole palette. The draw stays a single RNG step, so the traffic scenarios remain deterministic.
- [x] `RQ-175` Kerbs around the paving: where a paved cell has no paved neighbour the builder now
  emits a concrete kerb (0.11 m high, 0.22 m wide) with a top and an outer face, so the square
  and the filling station forecourt end in an edging instead of a bare seam against the grass.
  The cut around the roads produces the kerb automatically.
- [x] `RQ-176` Session audit: commit `2d97012e6215ff24e691edbcf94233a1599588f8` cloned fresh from
  `origin` and built against the dependency checkouts: every target builds, `ctest` passes all
  five registrations in 33 s (159 unit and scenario tests), both static checks are clean and the
  map validator reports no warnings. `scripts/capture_set.sh` reproduces the curated screenshot
  set in one command.


---

## 25. Phase 12 -- Living world (`LW`)

Started 2026-09-15 on top of the Phase 11 follow-up work. Four systems the owner asked for, in
this order: a day and night cycle, weather, working traffic signals and a larger world with more
villages and towns. The constraints of Phase 11 are unchanged: only CNA's XNA 4.0 public API and
project-owned code, no test may be removed or weakened, every defect fix carries a regression
test, and the static checks stay mandatory.

### 25.1 Task ledger

- [x] `LW-001` Solar model. `LightingRig` gained a clock: `SetTimeOfDay(hours)` places the sun
  for latitude 49.8 deg N and a declination of +20 deg with solar noon at 13:00 (Czech summer
  time), and derives the whole palette from the sun's elevation -- key light colour and strength,
  sky ambient, sky fill, ground bounce, fog colour and range, zenith and horizon colours. Below
  -1 deg the key light becomes a dim cold moon from the opposite side of the sky. Five tests in
  `tests/Render/LightingRigTests.cpp` cover the sun's path, day and night, the warm low sun, the
  moonlight and the baked-lighting scale.
- [x] `LW-002` Baked lighting follows the clock. The terrain macro texture, the ground shadows
  and the road and verge vertex colours stay baked under a fixed reference sun
  (`LightingRig::BakeReference()`, 10:30) -- `WorldRenderer` now holds that reference as
  `bakeRig_` instead of baking under whatever hour the game started at, which darkened the
  ground twice. Per frame, `LightingRig::BakedLightingScale` gives the ratio of the horizontal
  irradiance now to the irradiance at the bake, and `WorldRenderer::ApplyLighting` multiplies it
  into the terrain, road and tree effects. Known limitation: the direction of the baked shadows
  does not move with the sun.
- [x] `LW-003` Sky follows the clock. `SkyRenderer::Refresh` rebuilds the dome colours with a
  sunset glow around the sun's bearing, fades in 420 stars below -2 deg, draws a moon opposite
  the sun and dims the clouds as the light goes.
- [x] `LW-004` The car follows the clock. `VehicleMaterials::ApplyLighting` re-applies the rig to
  the lit, textured, paint and interior effects, scales the cockpit's interior light with the sky
  and rebuilds the paint's sky cube map when the sun has moved three degrees.
- [x] `LW-005` The clock itself. `SimulatorGame` advances `timeOfDayHours_` by `timeScale_`
  simulated seconds per real second (default 60x, so a day takes 24 minutes), re-applies the
  lighting when the sun has moved a quarter of a degree, and shows the time in the HUD. `--time
  <hh:mm>` (or decimal hours) and `--time-scale <x>` set it from the command line, the save file
  remembers both, and F6/F7 step the clock an hour back and forward while F8 freezes it.
- [x] `LW-006` Lit windows. `BuildingGenerator` splits the window quads of every building into a
  dark and a lit batch with a hash of the quad centre and the building seed (40 % of the framed
  windows, 35 % of the shop and block glazing), so the split is the same in every run and on
  every renderer. The lit batches carry a warm `nightEmissive` that `WorldRenderer` adds to the
  batch emissive in proportion to the rig's lamp factor, so the town lights up after sunset and
  goes dark again at dawn without a second draw of the same geometry.
- [x] `LW-007` Street lamps. `WorldRenderer::BuildLampLights` builds, per terrain chunk, a warm
  pool on the ground under each lamp (a 6x6 grid draped on the ground surface so it follows the
  camber and the kerb, 8.5 m across) and a glow at the lantern (two crossed cards, so no
  per-frame billboarding). They are drawn additively after the world pass, scaled by
  `LightingRig::LampFactor()`: 0 above six degrees of sun elevation, 1 once the sun is four
  degrees below the horizon. The pass costs about 2 ms of the 76 ms town frame on llvmpipe and
  nothing at all by day, when it is skipped outright.
- [x] `LW-008` Headlamp pool. `VehicleRenderer::DrawHeadlightPool` drapes a 12x10 grid on the
  road ahead of the car (2.2 to 26 m on low beam, 48 m on high beam) with a lateral and
  longitudinal falloff baked into the vertex colours, drawn additively through the car's own
  unlit vertex-colour effect. It fades in with the same lamp factor, so the beam does not show
  in daylight. The night floor of the palette was raised by half a stop at the same time
  (moonlight, night ambient and night sky fill) so that a moonlit street is legible without the
  lamps.
- [x] `LW-009` Weather model. `Core::WeatherState` (project-owned, no dependencies) holds a
  preset kind -- clear, few clouds, overcast, rain -- and the three continuous values the world
  is drawn and driven from: cloud cover, falling rain and how wet the road is. `Set` changes the
  preset and `Update` eases towards it, so a front moves in over a couple of minutes; the road
  soaks in about a minute and takes ten to dry, so it still shines after a shower. `--weather
  <name>` and the save file choose it, `F9` cycles it, and the HUD names it next to the clock.
- [x] `LW-010` Weather lighting. `LightingRig::SetWeather(cover, rain)` folds the weather into
  the palette the hour has just produced. Cloud moves the key light into the dome: the sun
  collapses to a twentieth of its strength under a solid lid and most of what it loses comes
  back as flat, slightly cool ambient and sky fill, so an overcast noon keeps about 80 % of the
  light on a horizontal surface while a wall facing away from the sun actually gains. Expressing
  it as a redistribution rather than an absolute grey is what keeps an overcast midnight dark.
  Rain takes another third out and greys the fog; both shorten the view. The lamps come on about
  three degrees of sun elevation earlier under a lid.
- [x] `LW-011` Weather sky. The cloud layer texture is regenerated from the cover (the noise
  threshold slides and the clouds darken as they thicken), the lid hangs lower when it is solid,
  and the stars, the moon and the sun disc fade out as the cover closes in.
- [x] `LW-012` Rain. `RainRenderer` keeps 900 streak cards in a 44 m slab that scrolls with the
  wind and wraps around the camera; the drops lean along the fall direction and face the viewer,
  and only as many of them are drawn as the rain deserves, so light rain is a subset of heavy
  rain rather than a different pattern. It costs about 0.35 ms per frame in town on llvmpipe and
  nothing when it is dry.
- [x] `LW-013` Wet road. Wet asphalt is darker and picks up the colour of the sky
  (`WorldRenderer::SetWetness`; the paint on it darkens less because it stays rough), and
  `Vehicle::SetRoadWetness` takes 30 % off the peak grip, which lengthens the 100 km/h braking
  distance by about a third. The audio gains a rain hiss (louder in the cockpit, where the drops
  land on the roof) and the spray a wet road throws up under the wheels.
- [x] `LW-014` Traffic signals. The map format gained a `signal` approach control and a node
  `signals` plan (green, amber, all-red, offset and the groups of roads that go green together);
  `RoadNetwork` assigns every approach to a group and `LaneGraph` carries it onto the
  connectors. `Traffic::AspectAt` is a pure function of the plan, the group and the elapsed
  time, so a scenario warmed up for n seconds always finds the same lights; `TrafficSystem`
  advances one clock for the whole map. Cars stop at red and at red-and-amber, treat amber as
  stop unless they are already too close to stop comfortably, and commit once they are within
  12 m on green so a change never leaves one halfway across.
- [x] `LW-015` Signal heads. `ObjectPlacement::PlaceTrafficSignals` puts a mast on the
  right-hand kerb at the stop line of every signalised approach, facing the traffic; the mast
  and housing are ordinary props (`signal_head`) drawn with the world, and `SignalRenderer`
  draws the three lenses -- all of them dark in one batch, then the lit ones in their colours
  with an additive halo that fades in as the light goes. The junction "U kaple" on the sample
  map is signalised (main road against Zahradní and Kostelní), and the map validator checks the
  plan: unknown roads, roads that miss the node, a road in two groups, a green under three
  seconds and a single-group plan that would never turn red.
- [x] `LW-016` A wider region. The sample map grew from 4.2 x 5.8 km to 6.4 x 7.6 km with four
  new places, written by `tools/maps/add_settlements.py`: **Březí**, a street village on the
  main road east of Lipová; **Podhájí**, a village on a new southern ring road from S2 round to
  E3; **Nové Město**, a small town in the north-east with its own square, three prefab blocks and
  a loop off the forest road; and **Kamenice**, a hamlet at the end of a new south-western road.
  254 more buildings, three new class III roads and three residential streets, boundary and
  priority signs, lamps, bus stops, two tree avenues, new hills, forests and fields, and a player
  spawn at each. The terrain grid went from 4 m to 5 m to pay for the area, so the chunk count
  rose only from 1518 to 1920. The script is additive and idempotent: it marks everything it
  writes with a `generated-by` key and replaces only that, so the hand-authored square, chapel,
  filling station and parked cars survive a re-run.
- [x] `LW-017` Traffic robustness for the bigger map. The new junctions found three real defects
  in the traffic AI, all fixed with the soak test (thirty simulated minutes) as the regression:
  a car turning across oncoming traffic only had to see a fixed 4.5 s gap, so it committed to
  turns it could not finish -- the gap now has to cover its own crossing time as well; a
  follower did not see a car that had just left its own lane onto a *different* connector, and
  drove into the back of it; and a pair wedged inside a junction had no way out, because the
  back-off needs room behind it and there is not always any -- a car that has stood still on a
  connector for one and a half times the deadlock time now creeps out, the lower id of a pair
  first.
- [x] `LW-018` Object fog cull. No object batch is submitted beyond the rig's `fogEnd`, where it
  is indistinguishable from the fog; worth about 7 ms of the Lipová frame now that other
  settlements stand on the horizon.
- [x] `LW-019` Weather transition cost. Easing a front in moves the cloud cover a little every
  frame, and the first cut re-applied everything on every step: the car's sky cube map was
  rebuilt over a hundred times during a two-minute transition and the 512 px cloud texture
  twenty-five times. The cube map now follows three degrees of sun or fifteen per cent of cloud
  (only the start-up and the manual clock keys force it), and the cloud texture ten per cent of
  cover.

### 25.2 Phase 12 audit (2026-09-15)

Commit `12fe99256d01cfc48e1d205bac13f082961256d0` cloned fresh from `origin` and built against
the dependency checkouts (Release, Ninja, OPENGLES3):

- every target builds (738 steps, no errors; the remaining warnings are the pre-existing
  `-Wdouble-promotion` ones in the audio synthesiser, the collision world and the benchmark
  accumulators, plus two in CNA itself),
- `ctest --preset opengles3` passes all five registrations in 48 s, `carsim_tests` runs 178
  unit and scenario tests in 34 s,
- `scripts/check_xna_only.py` (178 files, 544 XNA types) and `scripts/check_assets.py` are
  clean, and `carsim-mapvalidate content/maps/lipova` reports no warnings over 624 buildings,
  54 186 trees, 63 signs, 2 832 props and 150 parked cars,
- the audit binary runs the new features: `--spawn mesto --time 21:45 --weather rain` starts in
  Nové Město after sunset in the rain and captures a frame.

The renderer conformance record was re-measured on this map: OPENGLES3, OPENGL33 and SOFTWARE
submit the same 1 253 draw calls and 1.24 M triangles for the same frame, the image differences
are unchanged in character (mean 3.8/255 between the two GL paths), and the comparison sheets in
`docs/screenshots/renderers/` were regenerated. The curated screenshot set was re-shot and eight
pictures added (dusk, night, headlamps, signals, overcast, rain and the three new settlements).

## 26. Phase 13 -- Real hardware, visual realism & driving polish (`RH`)

Started 2026-09-15 on top of Phase 12. The brief for this phase is explicitly *not* more
breadth. The world, the systems and the test suite are large enough; what is missing is the
finish that makes them read as one coherent small driving simulator rather than a growing pile
of technical prototypes. The guiding rule for every task below:

> No more breadth until the existing depth is polished.

New functionality is admissible only where it fixes a defect, improves realism, visual quality,
usability or performance, or supports meaningful testing and tooling. Explicitly out of scope
for this phase: snow, hail, fog or storms, seasons, more weather modes, career/economy/missions,
police, pedestrians, multiplayer, dealerships, damage economy, more vehicles, more settlements
and more road network.

The Phase 11/12 constraints are unchanged: only CNA's XNA 4.0 public API and project-owned code,
no CNAEXT and no renderer internals, no test removed or weakened, every defect fix carries a
regression test, and the static checks stay mandatory.

### 26.1 Baseline measured at the start of the phase

Verified on branch `claude/cna-car-simulator-project-scx0ij` at `dcdb587`, in this container
(4-core x86-64, no GPU; llvmpipe software rasterisation under Xvfb):

- `cmake --build build/opengles3` is up to date and clean,
- `carsim_tests`: **178 tests in 46 suites, all passing** (35.5 s),
- `ctest --preset opengles3`: **5 registrations**, all passing
  (`carsim_unit_tests`, `xna_only_api_check`, `simulator_smoke`, `map_validate_lipova`,
  `asset_manifest_check`),
- `scripts/check_xna_only.py`: OK (178 files scanned, 544 XNA 4.0 types known),
- `scripts/check_assets.py`: OK (5 files listed, 1 asset),
- `carsim-mapvalidate content/maps/lipova`: OK, 0 warnings; 624 buildings, 54 186 trees, 63
  signs, 2 832 props, 150 parked cars; load 1.40 s.

### 26.2 The audit SHA discrepancy (`RH-001`)

The Phase 12 report advertised `dcdb587` as the final HEAD but recorded the fresh-clone audit
against `12fe992`. Investigated by diffing the two:

```
12fe992..dcdb587 = 1 commit, 5 files:
  docs/renderer-conformance.md           re-measured conformance record   (documentation)
  docs/screenshots/renderers/*.png       regenerated comparison sheets    (documentation)
  plan.md                                the audit record itself          (documentation)
  simulator/src/App/SimulatorGame.cpp    ApplyWeatherSettings             (CODE)
```

So the discrepancy was **not** documentation-only: `dcdb587` changed application code
(`ApplyWeatherSettings` now reports an unreadable weather name instead of dropping the result of
`WeatherFromName`) *after* the audit that was advertised for it. The change is small and benign,
but the advertised commit was never the verified commit, which is exactly the ambiguity this
task exists to remove.

Rule adopted for this phase and afterwards, recorded here so it outlives the session:

> **The commit advertised as final HEAD must itself be the commit verified from a fresh clone.**
> If anything is committed after the fresh-clone audit -- documentation included -- the audit is
> repeated on the new HEAD before any SHA is reported.

- [x] `RH-001` Audit SHA discrepancy explained, the code change in the gap identified, and the
  rule above adopted. The Phase 13 audit (section 26.6) repeats the fresh clone on whatever the
  final commit turns out to be.

### 26.3 Task ledger

#### P0 -- foundation

- [x] `RH-002` **One authoritative map-generation workflow.** `tools/maps/generate_lipova.py`
  had fallen several content passes behind the shipped map: re-running it destroyed the town
  square, the wayside chapel, the filling station, the parked cars, the extended forest and the
  `kostel` spawn. Rather than leave a script that must not be run, the generator was brought
  fully up to date and the two scripts were made ordered stages of one pipeline:
  - `tools/maps/build_map.py` is now the only entry point. It runs stage 1
    (`generate_lipova`, writes the whole map from scratch) then stage 2 (`add_settlements`,
    additive and idempotent), and can validate the result with `carsim-mapvalidate`.
  - Both stage modules refuse to run on their own without `--stage-only`, and point at
    `build_map.py`; the trap is closed rather than documented.
  - Stage 1 regained every piece of hand-edited content: the `square` and `yard` terrain
    regions, the eleven-point north forest polygon, the fourteen square/chapel/station
    buildings, the five limes, seventeen square and station props, all sixteen parked cars and
    the `kostel` spawn.
  - Stage 2 now also rewrites the map card description, measuring the road length (27 km) from
    the roads it has just written instead of repeating a stale "About 18 km" from stage 1.
  - `content/maps/lipova` was regenerated through the pipeline and committed. The regenerated
    map is semantically identical to what was shipped (verified field by field with a 1e-6
    tolerance); only key order and float formatting moved.
  - Documented in `docs/map-generation.md`, including which stage owns which content.
- [x] `RH-003` **Automated regeneration consistency test.** `build_map.py --check` rebuilds the
  map into a temporary directory, asserts that running stage 2 a second time changes nothing
  (idempotence), and diffs the result against `content/maps/lipova`, printing the offending
  hunks on failure. Registered as the ctest test `map_regeneration_check` (the sixth
  registration). Verified to fail on injected drift in either direction.
- [x] `RH-004` **Actionable diagnostic overlay.** The `F3` overlay was one run of text over the
  scene; it is now a panelled, sectioned read-out built only from project-owned instrumentation.
  It adds frames per second, the mean and **worst 1 %** wall-clock frame time over a 180-frame
  window, the update half split into vehicle / collision / traffic / audio, the draw half split
  per pass, the cluster and mirror cost with the mirror's resolution and update interval,
  simulated time and frames drawn, per-wheel slip and load, the weather state (cloud, rain,
  wetness), sun elevation and lamp factor, and drawn-versus-culled batch counts with the frame's
  draw calls and triangles. `MirrorView` gained `Width()`/`Height()` so the overlay can report
  what the mirror actually costs to fill. Toggled with `F3`, off in a normal session.
- [x] `RH-005` **Benchmark instrumentation.** `--benchmark` now labels its output with the scene
  it measured (clock, weather, camera, spawn), reports the worst 1 % wall-clock frame time
  beside the average, and breaks the update average into the same four stages as the overlay.
  The JSON gains `scene`, `frameMsWorst1pc` and `updateMsAvgSplit`.
- [x] `RH-006` **A deterministic route to drive.** `Traffic::RouteDriver` is an autopilot over
  the lane graph: it plans a route from a spawn through waypoints with `LaneGraph::FindRoute`,
  follows it with pure pursuit plus a cross-track term, and holds a target speed that falls for
  curvature and for the end of the route. It produces ordinary `DriverControls`, so the tyres,
  the clutch, the gearbox and the collision world all behave as they do under a human driver --
  nothing is teleported. `--route <name>` drives one and exits at its end (`--route-stay` keeps
  going). Routes are map data (`traffic.routes[]` in `traffic.json`, written by the map
  pipeline); the sample map defines `town` (1 656 m through the square, the parked cars and the
  signalised junction), `country` (994 m of main road, avenue and filling station) and `forest`
  (1 664 m of forest road). Four tests in `tests/Traffic/RouteDriverTests.cpp` drive every route
  of the shipped map with the real vehicle and assert it finishes, stays within 2.5 m of the
  lane centre, never puts a wheel in the air and produces identical results on a re-run.
- [x] `RH-007` **Repeatable benchmark scenarios.** `scripts/benchmark_suite.sh` runs eight
  scenes -- clear day, rain, clear night, rainy night, each from the exterior and the cockpit
  camera -- over a fixed route with a fixed traffic warm-up, a frozen clock and a fixed weather
  preset, and writes one JSON per scene. `scripts/benchmark_report.py` turns a directory of
  those into the Markdown tables that go into `docs/performance.md`, and with `--against` prints
  the percentage change against an earlier directory. `--quick` gives a 320x200 smoke run for a
  software rasteriser.
- [x] `RH-008` **Real-hardware validation procedure rewritten.**
  `docs/real-hardware-validation.md` is now a copy-and-paste procedure for the reference machine
  (Debian 13 / Radeon 780M) and any other PC with a GPU: what to install, how to confirm the
  driver is not llvmpipe, build and test commands, what each overlay section means, a manual
  driving checklist, the benchmark suite and the before/after comparison, the screenshot set, the
  renderer comparison with the exact vocabulary to report it in, sound, and what to send back.
  It states plainly that nothing on the page may be filled in from the container, and carries a
  results table whose only row is the container, marked as having no GPU.
- [x] `RH-009` **Driving-surface profile check in the map validator.** `carsim-mapvalidate` now
  walks every lane *and every junction connector* at one-metre steps and reports any place where
  the ground under the driving line steps more than 12 cm in a metre -- the discontinuity that
  throws a wheel into the air. It prints the worst step and its position whatever the result, so
  the number can be tracked. It found four real defects on the shipped map (below).
- [x] `RH-010` **Junctions meet their roads without a lip.** The driving-surface check found a
  47 cm step on a connector at Podhájí; three causes, all fixed:
  1. *Concave kerb fillets.* `RoadNetwork::BuildPatch` rounds each corner between two arms with a
     quadratic through the intersection of the two kerb lines. Where two arms leave at a sharp
     angle with very different setbacks, that control point falls on the node side of the chord
     and the fillet cut a notch into the paved area -- a turning car drove over the notch with a
     wheel off the pavement and off the flat junction plane. The fillet is now clamped to the
     chord: a straight kerb across the corner is the worst it may become.
  2. *Crossfall running into a flat apron.* The carriageway kept its full crown right up to the
     junction while the apron is planar, so the two met in a ridge of the crossfall's height.
     `RoadNetwork::CrownScale` now eases the crown and the shoulder fall to zero across the same
     setback-plus-ease run that `BuildHeights` already used for the centreline.
  3. *Centreline height on a sloped plane.* `SurfaceHeight` worked from the centreline, so on a
     junction plane with up to 10 % gradient a lane 1.4 m off centre was out by 14 cm.
     `RoadNetwork::SurfaceHeightAt(hit, point)` eases the carriageway onto `PlaneHeight` at the
     *actual point*; the terrain conformer, the road-surface query and the lane graph all use it.

  Measured over every lane and every connector of the shipped map (26.9 km of road, 52.4 km of
  lane, 148 connectors): the worst deviation between the ground and the designed road surface
  fell from **0.47 m to 0.036 m**, and the worst change over one metre to **0.022 m**. Nothing is
  now over the validator's 8 cm / 6 cm thresholds. Two regression tests in
  `tests/Map/SampleMapTests.cpp` hold both properties, and a third asserts no junction patch cuts
  back towards its node.

#### P1 -- how it looks and how it drives

- [x] `RH-011` **The paint reads as paint.** The car was one flat shade of its colour from every
  angle. `EnvironmentMapEffect` was already in place with a sky cube map, but the cube's alpha --
  which is what `EnvironmentMapSpecular` modulates -- carried only `pow(cos, 300)`, a sun disc
  about four degrees across. On a 64-pixel cube face that lobe covered two or three texels and
  bilinear filtering flattened it away, so no highlight ever reached the bodywork. The cube now
  carries three terms: the disc, a broad glare lobe around it, and a weak whole-sky term, at a
  128-pixel face size. Each material scales the glint it takes (`MaterialLook::envSpecular`):
  paint all of it, glass 0.30 (with the full glint a windscreen went white), a wing mirror 0.35,
  chrome 0.75. Paint's environment amount went from 0.22 to 0.30. The bonnet, roof and shoulders
  now carry a highlight that moves with the sun, and the glass reads as dark tinted glass with a
  sheen instead of a flat grey panel.
- [x] `RH-012` **The A-pillar no longer has a saw-tooth edge.** `CopyInnerShell` classifies whole
  skin quads, and the cabin's dark-to-light boundary was a world-space height test
  (`yc < belt + 0.14`) which cuts diagonally across the loft grid. Down the A-pillar, where the
  skin turns fastest, that produced a visible serrated join -- the single most "unfinished" thing
  in the cockpit view. The boundary now follows ring segment 12 (the belt ring itself), which is
  a line of the loft, so it comes out clean.
- [x] `RH-013` **The wing mirror is convex.** It was one flat quad, so it reflected a single
  direction of the sky cube across its whole face and read as a blank light-grey card beside the
  door. It is now a 5 x 4 patch with a 14 mm bulge and a 0.20 rad outboard aim, so the face
  carries sky at the top and ground at the bottom. A test in `tests/Render/ProceduralCarTests.cpp`
  holds the convexity and the aim.
- [x] `RH-014` **Headlamps that make night driving usable.** The pool on the road peaked seven
  metres ahead and was gone by twenty-six: from the driver's seat it read as a bright blob under
  the bumper with black road beyond it. The dipped beam now runs from 1.4 m to 40 m (main beam to
  65 m), is lit from just past the bumper, holds a plateau through the working band and fades with
  a soft cut-off, and kicks 0.16 towards the near verge as a right-hand-traffic beam does. The
  side falloff maps the beam centre onto the bias while both edges still reach zero -- dividing by
  one span left the right-hand edge lit, so the pool ended in a hard line across the road. The
  shape is now a free function, `Render::HeadlampBeamFalloff`, with a test in
  `tests/Render/NightLightingTests.cpp` covering the edges, the working band, the far cut-off and
  the asymmetry.
- [x] `RH-015` **Wet roads read as wet.** Wetness only darkened the asphalt and tinted it slightly
  towards the sky, which from the driver's seat was almost invisible. Wet asphalt is dark under
  your wheels and a mirror at the far end of the street, because the sky's reflection climbs with
  the grazing angle -- and down a road, distance *is* the grazing angle. A `BasicEffect`'s fog is a
  distance ramp, so the road surfaces are drawn a second time, additively, with a black diffuse
  and the sky as the fog colour: nothing near, sky far. Markings and gravel are excluded (paint
  and gravel stay rough when wet) and the pass runs only while the road is wet. Measured on the
  same frame, the road 60 m ahead goes from (28, 30, 34) to (55, 58, 63) while the road under the
  car is unchanged.
- [x] `RH-016` **Stars are points of light, not blue squares.** Each star was a hard-edged quad
  whose blue channel was pinned at 255 while red and green were scaled by brightness, so every
  faint star came out a saturated blue square. Stars are now soft fans -- a bright centre and a
  dark rim, drawn additively -- with all three channels scaled and a slight warm/cool spread.
- [x] `RH-017` **Profile and cut the rear-view mirror pass.** Measured first: on the `town` route
  in the cockpit the mirror cost **58.9 ms of a 219.5 ms draw** -- a quarter of the frame -- because
  it ran a second full world pass with the same 1 500 m far plane as the main view. The mirror
  image is a 200-pixel strip at eleven degrees, where nothing beyond a couple of hundred metres
  can be made out at all. `MirrorView`'s far plane is now 320 m and `WorldRenderer::Draw` takes a
  `maxDistance` that caps the terrain, object and tree ranges for the pass;
  `MirrorView::kDrawDistanceM` (300 m) is what the mirror passes. Re-measured on the same route
  and frame count: mirror **45.5 ms**, frame 254.5 -> 244.6 ms. No visible truncation in the
  mirror image -- traffic, trees, buildings and road all still read. The mirror is not removed and
  its resolution is unchanged; the overlay reports both.

#### P2 -- traffic, world and conformance

- [x] `RH-018` **Two real signal defects, found by a new soak.** A second soak
  (`tests/Traffic/TrafficSoakTests.cpp`) runs ten simulated minutes at the signalised junction
  "U kaple" with the player parked on the verge, and watches what the thirty-minute soak does not:
  the aspect at the moment each car *crosses the stop line*, whether any car faces the wrong way
  along its lane, and whether anything is spawned within 25 m of the player. It failed immediately,
  on two separate faults:
  1. *Creeping into the box.* A car caught past the stop line when the light changed was held by
     a virtual leader at the line, with the gap clamped to 5 cm. It did not stop -- it crept across
     the junction at half a metre per second, sitting in the box the whole time. A car already
     over the line now commits and clears the junction, which is what a driver does.
  2. *Commitment that survived a stop.* A car within 12 m of a green light sets `committed` so it
     does not stop halfway across on a change. But commitment was only cleared when the car
     reached the connector, so a car that committed on green and then had to queue behind another
     kept it while standing still -- and drove into the junction on red when the queue moved. This
     was the one genuine red-light violation in ten minutes. Commitment is now dropped when a car
     stops short of the line: having stopped, it obeys the next change like anybody else.

  Both soaks pass with the fixes; the thirty-minute one is unchanged.
- [x] `RH-019` **Profiles from earlier phases still load.** A test parses exactly what a Phase 11
  build wrote -- no clock, no weather, no mirror rate -- and asserts the mileage, transmission,
  vehicle and settings survive, that everything the file never had gets its default rather than a
  zero, and that writing it back and reading it again loses nothing. The save schema stays at
  version 1: Phase 13's one new field (`settings.graphicsQuality`) is additive with a default, and
  an older build ignores keys it does not know.
- [x] `RH-020` **Graphics quality tiers, three of them.** `Render::QualityTier` (low / medium /
  high) sets a world draw-distance scale, a vegetation scale, the mirror's draw distance and the
  mirror's update interval -- the four things measurement showed are worth trading. `high` is the
  default and is what every screenshot and every table was taken at; `--quality` overrides the
  saved setting for one run and the `F3` overlay reports which is in force. Measured on the
  `town` route in the cockpit: 240.4 / 221.4 / 204.9 ms wall clock for high / medium / low, almost
  all of it the mirror, because a software rasteriser is fill-bound and distant chunks cover few
  pixels. Recorded honestly in `docs/performance.md`, which also says the tiers should do more on
  a GPU and that this is one of the things the real-hardware run is for. Nothing within close
  range of the car changes at any tier.
- [x] `RH-021` **Driving feel and camera stability, measured rather than argued about.**
  - Wet physics: `tests/Sim/VehicleDriveTests.cpp` now measures and prints braking from 50, 90
    and 100 km/h dry and wet, a steady cornering hold, and 0-50 km/h. This build:
    **10.1 / 32.5 / 40.1 m dry** (0.98 g at all three speeds, so the model is consistent), wet
    **+40 %** at every speed (14.2 / 45.5 / 56.0 m), a steady cornering hold of 105 km/h dry
    against 90 km/h wet, and 0-50 in 4.8 s dry against 6.3 s wet. That is the 30 % grip factor
    doing exactly what a 30 % grip factor should: 1/0.7 = +43 % of distance. Noticeable, and not
    ice. The test bounds dry braking to 0.6-1.1 g, the wet penalty to +20-80 %, and requires the
    penalty to be the same at every speed.
  - Cameras: a test drives the whole `town` route with the autopilot and measures the
    frame-to-frame change in the *change* of each camera's position relative to the car -- its
    jerk, which is what a shimmer is. Chase **0.044 mm/frame²** mean, worst single frame 1.7 mm;
    cockpit 0.042 mm mean, worst 0.6 mm. Both are already smooth; the bounds are set at roughly
    four times the measurement so a future change that makes either twitchier fails here.
- [x] `RH-022` **Full-map validation, computational and by hand.** `carsim-mapvalidate` now also
  proves that the **whole lane network is reachable from every player spawn** (not just from lane
  0 -- a settlement you can spawn in but not drive out of would have passed before) and that
  **every named route plans from its spawn**, printing its length and step count. Together with
  the driving-surface check (`RH-009`/`RH-010`) that covers enumeration, connectivity,
  terrain/road alignment and spawn reachability over all 26.9 km. The part a machine cannot do --
  driving the three routes end to end, the nine spawns, the gravel track at night, and the two
  newest roads -- is written out as a checklist in `docs/real-hardware-validation.md` section 5b,
  with a note that anything a `--route` run reproduces becomes a regression test the same day.
- [x] `RH-023` **A day that is continuous, and the key light no longer turns over in one frame.**
  Two tests in `tests/Render/LightingRigTests.cpp` walk all twenty-four hours in all four weathers
  and compare the palette *at the cadence the game actually re-applies it*, which is what a player
  sees as a step. They found a real discontinuity: the key light changed hands from the sun to the
  moon at exactly -1 deg of elevation, so the colour jumped from black to moonlight in one step
  **and its direction flipped by 180 degrees at the same instant** -- every shadow and highlight
  in the scene turned over between two frames, twice a day. The hand-over now happens where the
  sun's own key is already exactly zero (its strength is clamped at the horizon) and the moon
  fades in over the following three and a half degrees.
  `LightingRig::RefreshStepDeg` also replaces the flat quarter-degree refresh threshold: 0.035 deg
  within eight degrees of the horizon, 0.12 deg through twilight, 0.25 deg the rest of the time,
  because the whole sky turns over in the twenty minutes around sunrise and sunset and barely
  changes at noon. With both, nothing in the palette moves by more than 0.02 (about 5/255 per
  channel) between two applications at any hour in any weather, and the lamps and the baked
  lighting scale by no more than 0.05.
  The second test prints the shape of a day for the hours the brief lists: 06:00 sun 6.0 deg,
  09:00 34.4 deg, 13:00 60.2 deg (solar noon), 17:00 34.4 deg, 20:00 6.0 deg, 21:30 -6.2 deg
  (lamps full), 00:00 -18.9 deg.
- [x] `RH-024` **Weather transitions audited the same way.** A test runs all twelve transitions
  between the four presets, both directions, from a settled world, and measures the largest
  per-frame step in cloud cover, rain, road wetness and the lighting palette computed from them.
  Worst step at 60 Hz: cover 0.00013, rain 0.00042, wetness 0.00012, palette 0.00042 -- fades of
  minutes, not switches. Transitions settle between **193 s and 548 s** of simulated time, which
  is a front arriving rather than a preset changing. No pops found; no change needed. The test
  stays as the guard.
- [x] `RH-025` **Forest edges taper.** A forest ended in a wall of full-grown timber: every road
  that runs beside one looked at a cut-out against the sky. Trees within twenty metres of a forest
  polygon's boundary are now scaled down to as little as 55 %, easing back to full height inside,
  so the canopy climbs from the edge the way a real wood does. Measured on the largest forest of
  the shipped map: the 507 trees within 8 m of the boundary average **0.65** scale against
  **1.00** for the 37 227 well inside it, and a test holds the ratio between 0.4 and 0.85. No
  extra geometry: the same 54 186 trees, at different heights.
- [x] `RH-026` **A signalised junction is painted like one.** Every arm of "U kaple" carried the
  give-way triangles (V 6a) that belong on a yield approach: the marking code skipped `Priority`
  and `RightHandRule` but not `Signal`, so the lights said one thing and the road said another.
  A signalised approach now gets the transverse stop line (V 5), which is the correct marking at
  traffic lights. A test in `tests/Render/RoadMeshBuilderTests.cpp` separates the two exactly by
  counting *lone* marking triangles in the band where the line is painted -- every other marking
  is a strip of quads, so a give-way triangle is the only marking that stands on its own: 0 over
  four signalised approaches, 15 over four yield approaches.

- [x] `RH-027` **Dashboard and night interior audited; nothing needed changing.** Captured the
  cluster target at 13:00 and 21:30 and compared them. The backlight follows the **ignition**, not
  the clock: `Color(120)` with the ignition off, full white with it on, which is how a car's panel
  lighting actually works and means the dial is equally legible at noon and at midnight once the
  car is running. Over that, every reading is live simulation state -- the speed and rpm needles
  track the drive, the fuel and coolant sub-dials track the tank and the thermal model, the gear
  label and the "AUTO"/"MANUAL" line follow the gearbox, the odometer, trip and instant
  consumption panel reads green like an LCD -- and the telltales (park, coolant, fuel, battery,
  engine, indicators, low and high beam) are grey until their state is true: at 21:30 with the
  lights on the high-beam telltale is green and, with the engine off, the battery and engine
  telltales are lit. In the night cockpit capture the cluster is comfortably readable without
  glaring, which is what the brief asks for; nothing here is a defect. Two taste questions were
  considered and rejected: European panels are usually lit warm rather than white, and many cars
  dim the panel when the headlights are on. Both would invalidate the cluster's own regression
  picture for no measurable gain.
- [x] `RH-028` **Street lighting audited; nothing needed changing.** At 23:00 on the square and
  along the main street the lamps throw discrete warm pools with a visible lantern glow, about
  40 m apart, and the pools do not merge into a uniformly lit map: the built-up areas read as lit
  and the country roads stay dark, which is what the brief asks for. The lamp positions come from
  `objects.props[]` and the pools from `WorldRenderer::BuildLampLights`, both scaled by
  `LightingRig::LampFactor()`, so they come on and go off with the same clock as everything else.
  Confirmed in `docs/screenshots/night.jpg` and `headlights.jpg`.

### 26.4 What was deliberately not done

Recorded so the next pass does not have to rediscover the reasoning.

- **No new weather, seasons, missions, economy, police, pedestrians, vehicles or settlements.**
  The brief for this phase ruled them out and nothing here needed them.
- **Building facades were not rebuilt.** (Partly addressed after Phase 13: framed windows now
  carry a mullion and transom, street facades of town houses get window surrounds with a head
  moulding, two-storey houses corner pilasters, cottages an eaves band.) Windows already carry a frame, a sill and a reveal
  within 420 m, and the settlements vary facade and roof colour per seed. Flat facades remain the
  most obvious "procedural" tell in a street scene at middle distance, but fixing it properly
  means more geometry on 624 buildings and belongs in a pass that can measure the cost on real
  hardware first. Listed as open work in `handoff.md`.
- **The terrain detail texture visibly tiles** (Addressed after Phase 13: the macro now carries
  23 m lush/thin patches and 61 m dry areas, and field furrows are faded out where the macro
  texel cannot hold them, which removes the moire stripes seen from the air.) when seen from a height (a roughly 40 m grid over
  open meadow). At driving height it is not noticeable, and breaking it up means touching the
  terrain macro that everything else is baked against. Listed as open work.
- ~~**The wet sheen does not reach the paved square or the filling-station forecourt**~~ Closed
  after Phase 13: squares, yards, pavements and kerbs carry a weaker sheen than asphalt, grass
  verges none; terrain, trees, buildings and car paint darken or gloss with the wetness, and
  standing water (a puddle mask, 16 m repeat) appears on asphalt once the road is soaked.
- **Traffic bodies were not changed.** Five silhouettes, two variants each and sixteen paint
  colours give 160 combinations, which reads as varied from a driving distance in the traffic
  and town captures. A bus or lorry class is still open work from Phase 12.
- ~~**Baked shadow direction still does not follow the sun**~~ Closed after Phase 13: the ground
  shadows are baked again on a worker thread (about 0.8 s) whenever the sun has turned 10
  degrees, and swapped in over a few frames; the web build keeps the load-time bake. Re-baking the
  terrain macro and the road vertex colours per hour is the only real fix and costs seconds per
  change of hour; it stays a documented limitation.
### 26.5 Against the success criteria

The brief for this phase ends with a description of what it should feel like to run the simulator
afterwards. Taken point by point, with what was actually done and what is only claimed as far as
it was measured. **Nothing here was verified on GPU hardware**: no such machine was available in
this session, so every "feels" claim is bounded by what a deterministic measurement can say.

| the brief asks that... | what was done | how it is known |
| --- | --- | --- |
| the car is pleasant and believable to drive | physics unchanged; the numbers it produces were measured and bounded | 100-0 km/h in 40.1 m dry (0.98 g, the same g at 50, 90 and 100), 56.0 m wet; 0-50 in 4.8 s dry, 6.3 s wet |
| steering does not feel artificial | not changed; the autopilot holds every route within 0.5-1.9 m of the lane centre through town, country and a forest hairpin | `RouteDriver` tests |
| clutch and manual driving work naturally | unchanged from Phase 11, still covered | clutch, stall, gear and drive tests |
| automatic transmission behaves sensibly | unchanged, still covered | transmission and drive tests |
| town driving feels coherent | junction surfaces, markings and signals fixed | see below |
| traffic behaves predictably | two soaks: 30 minutes for overlaps and stuck cars, 10 minutes at the signals | no overlaps, no stuck cars, no red-light crossings, nothing spawned within 25 m |
| signalised junctions look and behave correctly | two behaviour defects fixed (creeping into the box, commitment surviving a stop) and the marking corrected from give-way triangles to a stop line | soak test and marking test |
| roads look properly integrated into the world | three causes of a lip between a junction and its roads fixed | worst ground-to-surface deviation over 52.4 km of lane and 148 connectors: 0.47 m -> **0.036 m** |
| daytime looks good | paint, glass and mirrors now read as materials | before/after captures; `docs/materials.md` |
| sunset looks good | the key light no longer flips 180 degrees at dusk; the refresh cadence follows the sun | nothing in the palette moves more than ~5/255 between applications, at any hour, in any weather |
| night is genuinely driveable | the beam reaches 40 m with a plateau and a soft cut-off instead of a blob at seven metres | beam shape test; night captures |
| headlights are useful | as above, plus the right-hand kick to the near verge | `HeadlampBeamFalloff` test |
| dashboard is readable | audited at 13:00 and 21:30; backlight follows the ignition, every reading is live state | cluster captures |
| rain visibly changes the scene | unchanged rain, plus the wet sheen below | rain captures |
| wet roads look convincing | a grazing-angle sky sheen from a second additive road pass | road 60 m ahead (28,30,34) -> (55,58,63); road under the car unchanged |
| wet grip is physically noticeable | validated rather than preserved | +40 % braking distance at every speed, cornering 105 -> 90 km/h |
| the player car no longer looks like a prototype | paint highlight, glass sheen, convex wing mirrors | hero captures before and after |
| the cockpit no longer looks like a debug interior | the A-pillar's saw-tooth join and the blank grey mirror card are gone | cockpit captures before and after |
| towns have meaningful visual variation | **not changed this pass** -- 624 buildings already vary facade and roof colour per seed; flat facades at middle distance remain the biggest tell | recorded as open work in 26.4 and `handoff.md` |
| forest and countryside feel deliberate | forest edges taper instead of ending in a wall | edge trees 0.65 scale against 1.00 inside; test |
| performance is understood and measured | eight deterministic scenes, an overlay that splits both halves of the frame, before/after on the mirror, three tiers | `docs/performance.md` |
| the project can be regenerated, built and tested reproducibly | one map pipeline with a drift test; six ctest registrations | `map_regeneration_check`, the audit in 26.6 |

The one criterion not met is the one this session could not reach: **nobody drove it on a real
machine**. No GPU was available here, so "pleasant to drive" rests on measurements -- braking in
metres, lateral error in metres, camera jerk in millimetres, soak tests in simulated minutes --
and on captured frames from a software rasteriser. `docs/real-hardware-validation.md` exists so
that the person who does have the machine can settle it in half an hour, and its results table
is empty and says so.

### 26.6 Phase 13 audit

**The rule adopted in 26.2 is followed here: the commit advertised as the final HEAD is itself a
commit verified from a fresh clone.** Two fresh clones were run, not one.

**First clone — `c7d78b96c5c7f33d1631f5bdeb663f089679d185`** (the commit that re-shot the
screenshot set; the last commit carrying code). Cloned from `origin`, checked out, configured
Release/Ninja/OPENGLES3 against the dependency checkouts, and built from nothing:

- every target builds, exit 0. 96 warnings, all of them the pre-existing `-Wdouble-promotion` in
  the audio synthesiser, the collision world and the benchmark accumulators, plus the `__int128`
  pedantic one from Sharp Runtime and one `nodiscard` in `BuildCrossing`. **No new warning came
  from Phase 13**, and the one it did introduce (a shadowed local in `RouteDriver`) was fixed.
- `ctest --preset opengles3`: **all six registrations pass** -- `carsim_unit_tests` (60.8 s),
  `xna_only_api_check`, `simulator_smoke`, `map_validate_lipova`, `map_regeneration_check`,
  `asset_manifest_check`.
- `carsim_tests`: **196 tests in 47 suites, all passing** (178 in 46 at the start of the phase).
- `scripts/check_xna_only.py`: OK, 183 files scanned, 544 XNA 4.0 types known. **No CNAEXT, no
  renderer internals, no renderer-specific API entered the project.**
- `scripts/check_assets.py`: OK, 5 files listed, 1 asset. No asset was added this phase.
- `tools/maps/build_map.py --check`: OK -- the pipeline reproduces `content/maps/lipova` exactly
  (44 nodes, 16 roads, 624 buildings, 63 signs, 77 props, 16 parked cars), and stage 2 is
  idempotent.
- `carsim-mapvalidate content/maps/lipova`: **OK, 0 warnings**. 624 buildings, 54 186 trees, 63
  signs, 2 832 props, 150 parked cars placed. Worst deviation between the ground and the designed
  road surface over every lane and connector: **+0.036 m**; worst change over one metre:
  **0.022 m**; nothing over the 0.08 m / 0.06 m thresholds. All three routes plan from their own
  spawns (town 1 656 m, country 994 m, forest 1 664 m) and the whole lane network is reachable
  from every one of the nine player spawns.
- the game runs from the clean clone: `--spawn mesto --time 21:45 --weather rain --lights
  --cockpit` builds the world in 5.4 s and captures a frame -- cluster lit, rain falling, a street
  lamp burning, the cockpit readable.
- a route drives from the clean clone: `--route country` plans 993.9 m and runs 400 frames.

**Second clone — the commit that adds this record.** Only `plan.md` and `handoff.md` differ from
the first; no code, no content, no test. It was cloned fresh and re-verified the same way, and
that is the SHA reported as final. Nothing is committed after it.

Checklist from the brief, each item as verified above: map generation has one documented
workflow; re-generation destroys nothing; generation is deterministic and the additive stage is
idempotent; the final HEAD is known and was itself cloned fresh; the build succeeds; all tests
succeed; the static XNA-only check succeeds; the asset manifest check succeeds; the map validator
succeeds; the headless smoke test succeeds; both traffic soaks succeed; no CNAEXT and no renderer
internals entered the project; the player car, both gearboxes, the cockpit, both cameras, the
mirror, the weather, the clock, the save state, the traffic signals, the rain grip, the headlamps
and the dashboard night state all work and are covered by tests; the real-hardware procedure is
current; the screenshot set was re-shot on this build; every performance figure says which
environment produced it; the renderer record says exactly what was done per renderer; `README.md`,
`plan.md` and `handoff.md` match the repository; the branch is pushed; the working tree is clean.

## 27. Phase 14 — Architecture, World Fidelity, Traffic Rules, Audio & GPU Performance (`P14`)

This is the **current** product scope, superseding the historical initial non-goals in section 2
and the then-current deferred lists in sections 23–26: ordinary car driving, buses and lorries,
traffic and overtaking, traffic lights, pedestrians, player walking, helicopter mode, turbo,
ultra and ultra ultra turbo, visual damage, wipers, spray, rain, snow, fog and dynamic time of day
all remain supported. Do not disable or remove them to simplify a refactor. No missions, economy,
police gameplay, multiplayer or additional aircraft class belong to this phase. The XNA 4.0
public API boundary and its static check remain mandatory.

### 27.1 Measured starting state (2026-09-24, `18b7d468ecf3afdf62e515adf21262f1bc822fd2`)

- Branch `main`, clean against `origin/main` before Phase 14 changes. `cmake --build --preset
  opengles3 -j2`: no work, exit 0. `ctest --preset opengles3`: unit suite and four non-display
  registrations passed; display smoke lacked a video device in the shell. Repeated the display
  smoke with `SDL_VIDEODRIVER=offscreen`: passed in 16.54 s. The unit registration, including
  the 30-minute simulated traffic soak, took 104.27 s. Re-run after code changes.
- Largest units at this SHA: `SimulatorGame.cpp` 1916 lines, `TrafficSystem.cpp` 1618,
  `WorldRenderer.cpp` 1455, `ProceduralCar.cpp` 1427, `VehicleRenderer.cpp` 1107,
  `Vehicle.cpp` 959. These numbers identify ownership candidates, not line limits.
- Existing screenshots were inspected: town facades and cockpit trim read as broad flat surfaces;
  forest silhouettes repeat; road texture streaks at shallow angles on llvmpipe. The old curated
  images and three-renderer comparison remain the visual baseline until recaptured.
- `docs/renderer-conformance.md` and `docs/performance.md` report software-renderer timings only.
  In the comparable town chase scene the existing record is 1253 submissions, 1.242 M triangles
  and 166 ms draw submission on OPENGLES3 llvmpipe. These are **prior recorded** measurements,
  not new Phase 14 GPU results. The restricted shell hides `/dev/dri`, but the desktop `:0`
  outside it reports accelerated AMD Radeon 780M (Mesa 25.0.7, radeonsi, Debian 13);
  Xvfb `:199` reports llvmpipe. Real-GPU benchmarking is therefore possible through the
  desktop session and is required before any draw-call pass.

### 27.2 Task ledger

Each checked item requires evidence in code/tests/screenshots/docs in the same commit. Extract
one responsibility at a time: characterize, extract without changing the algorithm, build, run
targeted and full tests, exercise the runtime, then commit. Preserve the 30-minute traffic soak.

#### P0 — foundation and characterization

- [x] `P14-001` Audit branch, status, history, build, complete suite, screenshots, conformance,
  performance records and ownership of major systems; record actual baseline and environmental
  limits in 27.1. Acceptance: reproducible commands and no GPU claim from llvmpipe.
- [x] `P14-002` Reconcile README, handoff and plan with the accepted feature set without editing
  historical phase outcomes. Acceptance: no current-status prose says snow, fog, walking, heavy
  vehicles, pedestrians or overtaking are absent or deprecated.
- [~] `P14-003` Characterize fragile behavior before extraction: existing traffic soaks and mode
  tests retained; add deterministic coverage for any untested path selected for refactoring.
  Overtaking scenarios, flight/boost drive tests and fixed before/after renderer captures now
  guard the extracted paths. Further selected paths need their own characterization.
  Acceptance: pre-extraction failure would expose a behavior change.

#### P1 — coherent ownership and visible world

- [~] `P14-010` Extract coherent SimulatorGame responsibilities while retaining orchestration.
  First step: HUD, map, help and diagnostic drawing (341 lines, byte-identical definitions) moved
  into `SimulatorGameOverlays.cpp`; the deterministic help-overlay capture is pixel-identical
  before and after the extraction at 1280 x 720. Acceptance for completion: player-mode and
  environment responsibilities have clear ownership; walking, flight, weather, save and
  benchmark paths still work.
- [~] `P14-011` Extract traffic planning/queries/spawning or junction ownership incrementally.
  First step: the existing overtake and return-to-lane methods were moved unchanged into
  `TrafficOvertaking.cpp` (130 lines identical to the old definitions); four targeted overtake
  tests and the complete suite pass. The four body selection/spawn/despawn methods (120 lines)
  then moved byte-identically into `TrafficSpawning.cpp`; the population, visibility and
  heavy-vehicle junction tests and the complete six-part suite, including all soaks, pass.
  Four intersection-admission methods (`BoxClearFor`, heavy-body sweep, exit clearance and
  entry priority; 220 lines) then moved byte-identically into `TrafficJunctions.cpp`.
  Six focused junction and heavy-vehicle tests and the complete six-part suite, including all
  traffic soaks and the simulator smoke run, pass.
  Acceptance for completion:
  review remaining route/following/recovery ownership with signal and pedestrian tests plus
  all soaks still passing.
- [~] `P14-012` Separate useful vehicle, visual and world renderer responsibilities without a
  replacement architecture. The four existing scenery builders (buildings/props, lamp geometry,
  tree cards, signs) moved byte-identically into `WorldRendererScenery.cpp` while the render
  path remains in `WorldRenderer.cpp`. A fixed 1280 × 720 offscreen town capture before and
  after has identical SHA-256 `7e85dbe197249f600b147e77d0e1c47ea6b1ed5fda2b65e1ca48a49b7d462437`;
  the complete suite passed. The flight entry/exit and `StepFlight` methods then moved to
  `VehicleFlight.cpp` (flight stepping byte-identical); the four existing helicopter/boost
  drive tests passed and a fixed aerial screenshot has identical SHA-256
  `2ef7dd8520a9a577fbfeb4cedbf9dfb768c63f8992ffd542651b01561468e787` before/after.
  The 254-line player vehicle ground-shadow/headlamp-pool implementation was then moved
  byte-identically into `VehicleGroundEffects.cpp`; eight focused shadow/beam tests passed,
  and the fixed Radeon night capture retained SHA-256
  `d20fe5cacdaafdf2288ea91cab3114fa37faf97b8c5241f5f78fcbd6c733ad6a` before/after.
  Broader vehicle and road ownership are still open.
  Acceptance: normal car, boosts, flight, damage and all rendering
  modes retain their behavior; before/after captures and renderer checks show no regression.
- [~] `P14-020` Improve building facades and Czech settlement details using reusable parts.
  A shared framed/panelled door kit replaces flat door quads without adding a material pass;
  plaster no longer repeats a high-contrast stain motif over whole walls. The fixed town
  camera at `--view -78 9 -30 50 -6` was captured before and after on Radeon 780M and visually
  inspected. More facade variety and authored frontage context remain.
  The square-facing church tower now has shallow pilasters, a round window and a restrained
  stone portal; fixed Radeon before/after frames are in `docs/screenshots/phase14/`. The
  matched scene kept 1215 instrumented draws and added about 540 triangles (<0.04%).
  Acceptance: town and village before/after frames show depth, varied frontage and coherent
  street furniture at normal driving distance, with measured geometry cost.
- [~] `P14-021` Improve road surfaces, verges, vegetation and forest edges. Sparse deterministic
  resurfaced cuts now share the urban asphalt road mesh without changing collision or lane
  geometry. A fixed Radeon road view has before/after captures plus rain/snow checks. The
  first snow pass exposed double overlay on cuts; a coarse unpatched overlay mesh, only for
  repaired pieces, fixed it. The full-map test found 20 repaired pieces and 43,232 extra
  snow-overlay vertices (7.2% of the 599,752 detailed paved vertices), while the fixed town
  GPU capture retained 1215 instrumented draws and added 600 triangles (+0.041%). See
  `docs/performance.md`. Vegetation, forest edges, broader road texture/shoulder work remain.
  Acceptance: town, countryside and forest comparison images look less tiled/repetitive; no
  collision or lane geometry change; wet and snow variants remain coherent.
- [~] `P14-022` Improve cockpit geometry, materials, live cluster and day/night/weather
  readability. The 420 km/h scale now uses legible label spacing while keeping fine ticks;
  odometer/trip text fits its display, and the wheel position and night cabin illumination
  expose more of the controls. Fixed noon and night GPU captures are in
  `docs/screenshots/phase14/`. More geometry and weather review remain.
  Acceptance: curated cockpit and cluster captures at noon, sunset, night, rain,
  fog and snow; gauges remain driven by simulation state.
- [~] `P14-023` Polish pedestrians and walking movement/collision/camera. Shared rounded limb,
  torso and head geometry replaces box silhouettes; walking now accelerates and stops smoothly
  and a deterministic `--walk` capture path exists. Pedestrian draw cost is separately measured
  on the GPU. See `docs/screenshots/phase14/`; gait/clothing and more collision review remain.
  Acceptance: people no
  longer read as debug boxes and on-foot entry, exit, slopes, traffic and proximity regressions
  pass; helicopter access remains unchanged.

#### P2 — rules and sound

- [~] `P14-030` Model direction-aware centre-line and no-overtaking semantics in map data and
  render from the same source. `centreLine` now supports solid, dashed and both V 3 directions;
  `noOvertaking` is an independent road-wide restriction; the planner reads them and the
  renderer paints the corresponding strokes. IP 6 sign placements shared with the zebra renderer
  prevent starting a pass through a crossing. Parser, mesh and deterministic traffic tests pass;
  see `docs/research/czech-overtaking.md`. Acceptance for completion: authored local restriction
  ranges and signs, junction/crossing edge cases and rendered agreement in screenshots.
- [~] `P14-031` Extend the existing overtake planner for vehicle length, acceleration, safe
  return distance, oncoming speed, sight distance and weather; retain state hysteresis.
  Initiation now estimates passing time from the actual car's acceleration, accounts for
  oncoming closure over that time and scales clearance with wetness, snow and fog sight range.
  Clear, rainy, snowy, foggy, crossing, line-marking, oncoming and abort regression tests pass;
  road signs and phase-change decisions still need work.
  Acceptance: safe clear-road passes occur, risky cases reject, aborts never oscillate or
  overlap, and the 30-minute soak passes.
- [ ] `P14-040` Improve engine layers and load/RPM transitions; any recorded samples require
  verified redistribution rights and manifest provenance. Acceptance: reproducible audio
  captures show smooth starts, sweeps, shifts and overrun without clipping or loop seams.
- [~] `P14-041` Improve surface/tyre, wind, weather, traffic, cabin/exterior and helicopter
  mixing. First incremental step: the existing procedural rolling layer now uses wheel slip,
  authored ground roughness and road snow cover for bounded tread and scrub textures; deterministic
  tests show gravel, snow and sliding differ from dry asphalt. Up to six nearby AI vehicles now
  contribute stereo, distance-faded engine voices (heavier for buses/lorries); the cabin
  attenuates these more than the player's car, and the F3 overlay shows the active count.
  The helicopter rotor is now a separate project-owned DSP component with a filtered blade
  sweep and turbine layer over its preserved low thrum and 5/6/7/8 Hz mode cadence.
  Characterization tests cover fade, output bounds and turbo cadence. A 90-frame dummy-audio
  runtime with ~20 cars measured 0.37 ms mean mixer update; a separate 90-frame flight run
  measured 0.332 ms. Engine, wind, weather, cabin and rotor sound still need substantial
  listening and development.
  Acceptance: deterministic level/spectrum tests and listening review across modes;
  debug telemetry exposes layer gains without normal-play clutter.
- [~] `P14-042` Polish snow and fog visual/audio integration without removing either.
  Dense fog now culls alpha-tested tree chunks with the visibility horizon; fixed-camera GPU
  captures show fewer floating pale tree cards (33 to 6 visible tree batches). Snow gains a
  separate tyre texture. The forest fog edge and mixed low-light conditions still need review.
  Acceptance: captured rain, snow, fog, night combinations remain readable and performant.

#### P3 — measure, then optimize

- [~] `P14-050` Record real-GPU hardware/driver/renderer, FPS, frame, CPU update, project draw
  submission, object, submission, triangle, mirror, traffic and pedestrian counts for exterior,
  cockpit, dense town, forest, night, rain, snow, fog, pedestrian, walking, helicopter and a
  realistic worst case. Acceptance: same seed/settings/camera are reproducible; unavailable
  hardware is recorded as unavailable, never substituted with software figures.
  First eight town scenes measured on Debian 13/Radeon 780M/Mesa 25.0.7/OPENGLES3 at
  `2fa9ddd`, documented in `docs/performance.md`. Clear day: 10.53 ms exterior and 16.26 ms
  cockpit project draw submission; mirror 5.33 ms. Night wall-clock FPS is invalid where the
  desktop throttled the unfocused window. Supplemental forest, snow, fog, pedestrian, walking
  and aerial views are now documented; pedestrian main-view counts are exported separately.
  Counts still omit some passes, and worst-case, controlled whole-frame and unthrottled night
  measurements are open.
- [ ] `P14-051` Identify the dominant real-GPU costs and implement *only justified* targeted
  batching/LOD/culling. Acceptance: before/after on identical scenes reports absolute and
  percentage changes plus image and memory trade-offs. No unmeasured global batching rewrite.
- [~] `P14-052` Recheck all available CNA renderers after visual/performance changes through
  the public XNA API. Acceptance: equivalent screenshots inspected as well as draw counts; no
  renderer-specific application branch. On the real Radeon 780M desktop, OPENGLES3 and the
  now-available Vulkan RADV path both built and rendered the same fixed town scene with
  identical 1215 project submissions and 1,450,306 triangles. Town, cockpit and live cluster
  captures were compared visually and by pixel difference; see `docs/renderer-conformance.md`.
  OPENGL33 also produced byte-identical town, cockpit and cluster captures to OPENGLES3 on
  the Radeon. SOFTWARE rendered the same 1215 submissions/1,450,306 triangles offscreen;
  town, cockpit and cluster captures were visually inspected and pixel differences recorded.
  Repeat this checkpoint after later Phase 14 rendering or batching changes.

#### P4 — final audit

- [~] `P14-060` Run clean configure/build, complete tests and static checks, map validation,
  smoke, 30-minute traffic soak, representative runtime and practical ASan/UBSan core suites;
  fix findings. The normal six CTest registrations pass. The project-only `asan-ubsan` preset
  passed 213 core tests in 640.35 s with leak detection, including all three long traffic
  soaks; it found a real dangling `VehicleDefinition` reference from a temporary, fixed by
  owning an immutable definition. See `docs/sanitizers.md`. Final clean/fresh-clone and
  broader renderer checks remain.
  Acceptance: command log and exceptions are explicit.
- [ ] `P14-061` Curate before/after screenshots and update architecture, audio provenance,
  performance, conformance, README and handoff. Acceptance: docs distinguish measured GPU
  results from software runs and agree on accepted features.
- [ ] `P14-062` Commit and push logical increments; from the *last* pushed SHA make a genuinely
  fresh clone, configure, build, run all checks and representative scenes. Acceptance: clean
  working tree and exactly one advertised final SHA, audited after the final commit.
