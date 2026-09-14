# Handoff: cna-car-simulator

Written for an AI agent (or a person) who picks this project up in a fresh context. Read this
file, then `plan.md` (the authoritative task ledger; section 24 is the current phase) and
`README.md`. Everything below is verified against the repository state at the time of writing;
re-verify with `git log` and the ledger before acting.

## What this project is

A small, realistic passenger-car driving simulator in a fictional Czech landscape, C++23,
built on the **XNA 4.0-compatible public API** of the CNA framework (branch `next`) and Sharp
Runtime (branch `next`). One car (Lipan 1.2, procedural), one map (Lipová), ambient traffic,
cockpit with live cluster and mirror, procedural audio. No missions, no economy.

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
tests/       GoogleTest suites, registered in tests/CMakeLists.txt (151 tests at present)
tools/       map generator/validator, font atlas generator, simulation tracer
scripts/     run_headless.sh, check_xna_only.py, check_assets.py
docs/        api-boundary, framework-findings, map-format, vehicle-physics, audio-design,
             materials, cameras, performance, renderer-conformance, real-hardware-validation,
             research/, screenshots/ (curated set, m10-baseline/, renderers/)
plan.md      ledger; section 24 = Phase 11 "Realism & Production Quality" (24.4 = record)
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
- Spawns in `content/maps/lipova/traffic.json`: `square` (east-bound in town), `forest`
  (forest edge, heading NNW), `fields` (avenue through the fields), `east` (main road east of
  town), `kostel` (added last; 90 m west of the church junction E1, east-bound, untested in a
  capture yet).
- `--cockpit` (+ `--screenshot-cluster file`), `--chase-yaw <deg>` (positive = orbit to the
  car's right), `--chase-distance <m>` (INTEGER, `5.5` is rejected), `--view x y z heading pitch`
  (fixed camera; y is absolute, terrain is not flat, check for underground views),
  `--eye dx dy dz yaw pitch` (cockpit eye offset), `--lights`, `--benchmark`,
  `--benchmark-json file`, `--mirror-every n`, `--debug-overlay`.
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

## State of Phase 11 (plan.md section 24)

Done and committed (ledger `[x]`): RQ-001, 011-014, 020, 021, 030-032, 040, 041, 050, 051,
060, 061, 070, 071, 080, 081, 090, 100, 120, 121, 130, 131. RQ-010 is `[~]` (hero car: done
except items noted in the ledger). Last commits:

- `5fa7dd3` cameras, mirror interval, per-pass instrumentation and benchmark JSON, audio layers,
  driving-feel tests.
- `dc48f78` chase camera orbit fix + regression test, traffic spawn-visibility test, renderer
  conformance and real-hardware validation docs.
- The commit carrying this file: curated screenshot set (RQ-140), README tables and status,
  the `kostel` spawn.

Open:

- **RQ-140 polish (optional)**: the intersection tile is a fixed-camera view of the square
  junction if the chase-camera approach capture (`--spawn square --frames 840 --auto-drive 14`)
  or a `--spawn kostel --frames 480 --auto-drive 8 --traffic-warmup 40` capture was not ready;
  either would make a better picture. A forest picture deeper in the forest (chase camera,
  `--spawn forest --frames 720 --auto-drive 12`) was also in flight. The "lights" capture was
  dropped from the README: lit headlamps are only subtly brighter in daylight (a possible
  material tweak: stronger emissive on `LampHead` when on).
- **RQ-150 final audit** (not done): fresh clone of the branch, configure with the dependency
  paths above, build all targets, `ctest --preset opengles3`, both static checks, confirm
  README and plan.md agree, then record the verified SHA and the ctest summary in plan.md
  section 24.4 and mark RQ-150 `[x]`. A fresh-clone build of `dc48f78` was started in the
  session scratchpad but its result was not recorded; repeat it on the final commit.
- Known cosmetic items not in the ledger: the town square is a lawn with few buildings around
  it; traffic body diversity is visible but the palette is small; sun elevation is fixed.

## Working conventions that kept things sane

- Every rendering change: capture before/after, compare pixel samples or half-size images,
  record what changed in plan.md 24.4 in the same commit.
- Every defect fix: a regression test in the matching `tests/` suite, registered in
  `tests/CMakeLists.txt`; new sources registered in `simulator/CMakeLists.txt`.
- Keep temporary diagnostics (environment-variable switches, dumps) out of commits.
- Docs to keep in sync when touching an area: `docs/materials.md` (car looks),
  `docs/cameras.md`, `docs/performance.md` (numbers per scene), `docs/audio-design.md`,
  `docs/renderer-conformance.md`, `docs/real-hardware-validation.md`.
