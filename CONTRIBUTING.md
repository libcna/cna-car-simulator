# Contributing

How to build, test and change the simulator without breaking what holds it together. Start
with `README.md` for what the game is and `ROADMAP.md` for what is open.

## Hard rules

1. **XNA 4.0 public API only.** Application code uses CNA through `Microsoft::Xna::Framework::*`,
   plus Sharp Runtime, project code and the C++ standard library. No CNAEXT (`CNA_CNAEXT` stays
   `OFF`), no CNA renderer internals, no renderer-specific or platform APIs (EasyGL, Vulkan,
   OpenGL, DirectX, SDL graphics). `scripts/check_xna_only.py` (CTest `xna_only_api_check`)
   enforces this against the XNA 4.0 type list in `scripts/xna4_types.txt`
   (`scripts/generate_xna4_type_list.py`); the reasoning is in `docs/api-boundary.md`. Do not
   loosen the checker to make a change pass.
2. **No renderer-specific branches.** If a renderer misbehaves, reduce it to a reproduction,
   record it in `docs/framework-findings.md` and fix or report it in CNA.
3. **Legal assets only.** Almost everything is generated in code. Every external file is listed
   in `assets/manifest.json` with author, source, licence and SHA-256, summarised in
   `assets/ASSETS.md` and checked by `scripts/check_assets.py` (CTest `asset_manifest_check`).
   Recordings must also pass the import gate in `docs/research/recorded-engine-sources.md`.
4. **Tests are not removed or weakened.** A defect fix comes with a regression test. Before
   refactoring, characterise the behaviour you are moving, then move it without changing the
   algorithm. The 30-minute traffic soak must keep passing.
5. **Accepted features stay.** Heavy vehicles, overtaking, signals, pedestrians, walking, the
   helicopter, turbo modes, visual damage and all weather are product scope (`ROADMAP.md`).
6. **The map is generated output.** Change the stages in `tools/maps/`, run
   `python3 tools/maps/build_map.py`, and commit the regenerated `content/maps/lipova` with them;
   CTest `map_regeneration_check` fails when they drift (`docs/map-generation.md`).
7. **History is not rewritten.** No force-push to `main`.

## Build, test, check

CNA (`next`), Sharp Runtime (`next`), `easy-gl` and `meta-gl` are sibling checkouts; see the
README. Pass `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache` on the
first configure if you use ccache -- a fresh CNA build takes minutes.

```bash
cmake --preset opengles3                        # also: opengl33, vulkan, software, debug, asan-ubsan
cmake --build --preset opengles3
env -u DISPLAY -u WAYLAND_DISPLAY ctest --preset opengles3   # six registrations, headless
python3 scripts/check_xna_only.py && python3 scripts/check_assets.py
python3 tools/maps/build_map.py --check         # shipped map == generator output
build/opengles3/bin/carsim-mapvalidate content/maps/lipova
```

The registrations are `carsim_unit_tests` (GoogleTest `carsim_tests`, including the traffic
soaks), `xna_only_api_check`, `simulator_smoke`, `map_validate_lipova`,
`map_regeneration_check` and `asset_manifest_check`; select with `-L unit|static|content|display`.
The sanitizer build is described in `docs/sanitizers.md`. New sources are registered in
`simulator/CMakeLists.txt`, new tests in `tests/CMakeLists.txt`.

## Layout

```
simulator/include/CarSim/<Area>/, simulator/src/<Area>/
    Core  Sim  Map  Collision  Traffic  Audio  Input  Render  App
content/   vehicles/*.json, maps/lipova/*.json (generated), fonts/, audio/
tests/     GoogleTest suites per area, registered in tests/CMakeLists.txt
tools/     maps/ (map pipeline), mapvalidate, simtrace, audio_preview, font atlas
scripts/   run_headless, capture and benchmark scripts, the two static checks
docs/      design notes per subsystem, research/, screenshots/ (README pictures)
assets/    external files, manifest.json, ASSETS.md
```

`carsim_core` (simulation, map, collision, traffic, audio DSP) must stay renderer-free so its
tests run without a display; everything touching `GraphicsDevice`, audio devices or input is in
`carsim_render`.

## Working habits

- A rendering change gets a before/after capture of the same fixed state (`--lockstep
  --time-scale 0 --weather ... --view x y z heading pitch`) and a check on all four renderers
  (`docs/renderer-conformance.md`). Label every image and number with renderer, GPU, driver and
  resolution; llvmpipe numbers are not GPU numbers.
- Keep the matching doc current: `docs/materials.md` (car looks), `docs/cameras.md`,
  `docs/performance.md`, `docs/audio-design.md`, `docs/renderer-conformance.md`,
  `docs/map-generation.md` (anything under `tools/maps`).
- Keep temporary diagnostics (environment switches, dumps) out of commits. Captures, benchmark
  output and listening renders go under `build/`, which is not tracked.
- Never open windows on somebody's live desktop: `scripts/run_headless.sh` uses an existing
  `DISPLAY` if one is set, so unset `DISPLAY` and `WAYLAND_DISPLAY` to get its Xvfb on `:99`, or
  use the hidden GPU path in `docs/performance.md`.

## Gotchas

**Running and capturing**

- `content/` is copied next to the binary by a post-build step. After editing JSON, rebuild the
  simulator target or pass `--content content`.
- `--lockstep` runs one 1/60 s step per drawn frame: `--frames 480` is 8 s of simulated time.
  `--time-scale 0` freezes the sky, which every deterministic capture needs. `--view`'s height is
  absolute -- the terrain is not flat, so check for views from underground. `--chase-distance`
  takes an integer. `cna-car-simulator --help` lists every switch.
- llvmpipe needs 0.2–0.5 s per town frame and SOFTWARE several seconds; run long captures in
  the background. The hidden SDL offscreen GPU surface is fixed at 800 × 480.
- The sample-map load test (< 6 s) can fail on a heavily loaded host; rerun it alone before
  suspecting a regression. LeakSanitizer needs to inspect threads and fails inside restricted
  sandboxes; run it outside rather than disabling leak checks.

**Geometry and cameras**

- Right-handed coordinates, +Y up, north is -Z. Physics runs in fixed 120 Hz sub-steps inside
  `Vehicle::Update`; traffic, collision and audio run per frame.
- Front faces are clockwise (XNA). The outward normal is `-cross(b - a, c - a)`;
  `MeshData::SignedVolume()` is negative for an outward mesh and `OrientOutward()` fixes an
  inside-out loft. Ground meshes must wind like the terrain grid, `(x,z) -> (x,z+1) ->
  (x+1,z+1)`, or they are culled.
- Chase camera: yaw = `atan2(-fwd.x, -fwd.z)`, behind = `(sin, 0, cos)`, right =
  `(cos, 0, -sin)`.
- Details on the car body are projected onto the skin (`SkinGrid::Sample`, `FrontFacePoint` for
  the nose): the loft sweeps inwards at nose and tail, so a centre-line offset floats beside the
  bumper. Lamp lenses are decals cut from the skin in UV space; a skin quad becomes a recessed
  housing only when all four corners lie inside the lens, or black notches appear.

**Lighting and effects**

- The terrain macro texture and the road and verge vertex colours are baked under
  `LightingRig::BakeReference()` (10:30, clear) and scaled per frame by `BakedLightingScale`;
  only the cast shadows in them are re-baked for the real sun. `WorldRenderer` keeps that rig as
  `bakeRig_`; baking under the live rig darkens the ground twice -- the first thing to check
  when the ground looks wrong at an odd hour.
- Cloud redistributes light (the key collapses into ambient and sky fill) rather than mixing
  toward a fixed grey; a test guards an overcast midnight. `LoadContent` ends with
  `RefreshLighting(true)` because clock and weather are read before the renderers exist.
- Signals: `Traffic::AspectAt` is a pure function of plan, group and time, so warmed-up
  scenarios are reproducible.
- `DualTextureEffect` computes `detail × macro × 2`, as XNA does; the terrain factors were tuned
  against that. Sampler state is per slot and persists: the terrain sets slot 0 to
  `AnisotropicWrap` (detail) and slot 1 to `LinearClamp` (macro), which lets the horizon apron
  reuse the macro by clamping.
- `EnvironmentMapEffect` adds `EnvironmentMapSpecular × cubemap alpha` (the XNA formula), so the
  sky cube map's alpha carries the sun-highlight mask. A loose PNG is not premultiplied for
  `SpriteBatch` (as in FNA), so `tools/fontatlas.py` writes premultiplied atlases.
- Custom shaders are unavailable and `SpriteFont` cannot be built at runtime, hence the stock
  effects and the project's bitmap fonts. `GetBackBufferData`, render targets with depth,
  32-bit indices and instancing need the HiDef profile. Effect pass collections are iterated by
  index (their `begin()`/`end()` are CNAEXT).
- Custom vertex structs need a `VertexDeclaration`: CNA now refuses, at compile time, a user
  draw of a non-stock struct without one. The terrain snow pass uses a compact
  `VertexPositionTexture` copy because Vulkan rejected the 40-byte dual-UV layout there.
- Several older CNA observations are fixed or were misdiagnosed: untextured lit `BasicEffect`,
  unreliable stencil clears, `DrawUserPrimitives` after the world pass, "layouts are selected
  by stride". `docs/framework-findings.md`, section 3.4a, has the current state -- read it
  before relying on any workaround, and note that on Metal `DualTextureEffect` still uses the
  first UV set for both textures (CNA METAL-282).
