# Roadmap

Where the simulator stands, what it deliberately does not do, and what is worth doing next.
The detailed history of how it got here (the phase-by-phase task ledger and its acceptance
evidence) is in git history up to September 2026; this file keeps only what is still true.

## Scope

The simulator is about driving: one car, one region, traffic, weather and time of day.
Missions, jobs, deliveries, an economy, career or money, buying cars, police gameplay,
multiplayer, VR and a huge open world are out of scope, as is a second aircraft class next to
the helicopter. Everything the README lists -- buses and lorries, overtaking, traffic signals,
pedestrians, walking, the helicopter, the turbo modes, visual damage, rain, snow, fog, wipers,
spray and the day/night cycle -- is accepted functionality: do not remove or disable any of it
to simplify a refactor.

## Known limitations

**Content**

- One drivable car (the procedural Lipan 1.2) and one map (Lipová). No legally redistributable
  model of a real Czech car was available, so the car is generated in code; the vehicle
  definitions are data, and CNA's glTF import path would let a licensed model be added later.
- No mechanical damage: hard knocks dent the body and break lamps, nothing else.
- Traffic may overtake slower AI traffic but never the player: a car behind a player who stops
  in its lane waits.
- Town frontage and public spaces are still the most visibly procedural part of the world at
  middle distance, despite the door, shop, shutter, limewash and balcony variants and the square
  promenade.
- Winter is a tint and an overlay rather than a model: tree crowns, bushes, fields, roads and
  roofs take snow, but bark, lower boughs and most low props stay dark, and there is no winter
  branch geometry or undergrowth transition.

**Rendering (XNA 4.0 stock effects only)**

- No custom shaders: CNA's `Effect` needs compiled Direct3D 9 bytecode and no HLSL compiler is
  in the toolchain. Hence no normal maps and no shadow maps; see `docs/framework-findings.md`.
- Lighting is baked under a 10:30 clear-sky reference and scaled per frame. Shadows cast by
  buildings and trees are re-baked on a worker thread whenever the sun has turned 10 degrees,
  but the terrain's slope shading keeps the reference sun, and the web build (no worker
  threads) keeps the shadows baked at load.
- The terrain detail texture still tiles visibly when seen from height (helicopter), though
  not at driving height.
- The ground-shadow swap after a daylight re-bake is the worst remaining update hitch
  (about 36 ms in a matched run).

**Measurement**

- All GPU figures come from one machine, an AMD Radeon 780M iGPU with Mesa `radeonsi`, and time
  CPU submission, not GPU execution (`docs/performance.md`).
- The hidden-GPU scripts are limited to an 800 × 480 surface and check for `radeonsi`.
- Still unmeasured: a focused 1280 × 720 night run, a realistic worst case as a whole frame,
  the quality tiers on a GPU, and the texture upload during a live change of snow cover.
- The manual real-hardware checklist (`docs/real-hardware-validation.md`, section 4) has never
  been recorded as a complete pass.

**Renderers**

- Four desktop renderers are routinely compared (`docs/renderer-conformance.md`). The web
  build runs, but has no recorded side-by-side comparison; Windows builds with MSVC, with no
  DirectX renderer comparison; Metal is untried and would draw the terrain wrongly (below).
- On Xvfb with Mesa llvmpipe the OPENGLES3 path shows a coarse tiled ground pattern that the
  Radeon and the other renderers do not. It predates the change it was noticed with and has not
  been isolated to CNA, EasyGL or llvmpipe.

## Risks to keep in mind

- **Renderer behaviour behind the XNA API.** Several workarounds in the code answer CNA
  behaviour observed in September 2026. Most of it has since been fixed or explained upstream
  (`docs/framework-findings.md`, section 3.4a); a workaround is removed only after checking all
  four renderers.
- **Procedural look.** The car, buildings and vegetation are generated. Realism comes from
  geometry detail, materials, environment mapping and baked light, not from scanned assets.
- **Traffic deadlocks.** Right of way is antisymmetric, junction boxes are kept clear, and a
  deadlock breaker and stand-off back-off exist. The 30-minute soak passes on 100 seeds
  (`CARSIM_SOAK_SEED`); keep it passing after any traffic change.
- **Headless environments hide GPU issues.** llvmpipe filters, rasterises and times
  differently from real hardware. Label every image and number with its environment.
- **CNA build time.** A fresh CNA renderer build takes minutes: use ccache, and reuse build
  trees and the SDL prebuilt cache (`CNA_SDL_PREBUILT_ROOT`).

## Next steps

Roughly in order of value for effort.

1. **Drop workarounds CNA no longer needs** (`docs/framework-findings.md`, section 3.4a), each
   with before/after captures on all four renderers:
   - `VehicleMaterials::Lit`'s 4 × 4 white texture (`VehicleRenderer`): an untextured lit
     `BasicEffect` now renders correctly.
   - `DualTextureEffect` does double `detail × macro`, as in XNA; the terrain macro factors
     were tuned by eye against that doubled result, so retune them only with captures.
   - Consider the 36-byte vertex-coloured lit layout, which EasyGL now draws, for per-vertex
     baked light instead of the second UV set -- only after Vulkan has been checked.
   - Stencil clears and user-primitive draws were not reproducible on current CNA; stencil
     shadows are possible again if they are worth their cost.
2. **Metal:** CNA's Metal renderer samples both `DualTextureEffect` textures with the first UV
   set (CNA METAL-282, open). Until it is fixed, the terrain macro lighting will be wrong there.
3. **Performance:** a focused 1280 × 720 night run and a whole-frame worst case; measure the
   quality tiers on a GPU; time the snow-cover texture upload; shorten the daylight
   shadow-swap hitch. Only then consider the levers not yet taken: batching building and prop
   batches per chunk with a texture atlas, instanced trees with a far single-quad LOD, one road
   buffer per surface material. The mirror costs about a third of the cockpit's submission;
   cheaper mirror settings saved little when measured.
4. **Memory:** the Vulkan-compatible snow terrain copy (52.9 MiB, allocated even in clear
   weather) could be built on first snow instead.
5. **World:** wider facade and public-space variety in the town and villages; low props and
   bark under snow; forest floor and road-shoulder detail; terrain tiling seen from height.
6. **Car:** body polish -- nose and tail sculpting, bumper split lines, lamp housings, arch lips,
   seam lines -- or a licensed real-car model if one becomes available, through the data-driven
   vehicle definition. Mechanical damage would be a new system, kept separate from collision.
7. **Traffic:** overtaking a stopped or slow player; more body variants as vehicle definitions.
8. **Code ownership:** `SimulatorGame` still coordinates save and benchmark handling, and
   `TrafficSystem::Update`'s per-vehicle step still combines routing, following, pose update
   and collision recovery. Split them only with characterisation tests first, as the earlier
   extractions did.
9. **Renderers:** isolate the Xvfb GLES3 tiling; record a web and a Windows renderer
   comparison.
10. **Maps:** a second map, and a binary map cache or chunk streaming only if loading outgrows
    its budget (`docs/map-format.md`).
11. **Real hardware:** run the full `docs/real-hardware-validation.md` checklist on at least one
    more machine, preferably with a discrete GPU, and record it.
