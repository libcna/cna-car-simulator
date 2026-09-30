# Performance

What the simulator costs, how to measure it reproducibly, and which levers are already in use.
Every number below names the machine, renderer and resolution that produced it; a frame time
without that context means nothing. Git history holds the full per-change measurement log
(before/after tables, JSON records and captures) up to September 2026.

## Budgets

| Budget | Where it is enforced |
| --- | --- |
| 60 Hz (16.7 ms a frame) at 1280 × 720 on a mid-range GPU, `high` tier | measured by hand with the scripts below; Xvfb/llvmpipe is for correctness only |
| Update half (physics, traffic, collision, audio) about 1 ms or less, no single-frame hitch | working target, checked by hand with F3 and `--benchmark` (`updateMsMax`); see the shadow re-bake note below |
| Sample map load below 6 s (30 s in the sanitizer build, 60 s in MSVC Debug) | `SampleMap` test in `tests/Map/SampleMapTests.cpp` |
| Player car body below 60,000 triangles | `tests/Render/ProceduralCarTests.cpp` |

`docs/map-format.md` reserves a binary map cache for the day loading exceeds its 2 s design
target; it currently does not warrant one (map data about 1.4 s, world geometry 4–5 s).

## Measuring

**In the game.** `F3` shows frames per second with the worst 1 %, the update and draw halves
split by stage and by pass, the mirror's cost and size, drawn-against-culled batches, and the
audio mixer's gains. `--benchmark` prints the same statistics at exit (30 warm-up frames are
excluded) and `--benchmark-json <file>` writes them. `cna-car-simulator --help` lists the
controlled-capture switches (`--no-mirror`, `--no-wing-mirrors`, `--mirror-width`,
`--mirror-distance`, `--mirror-every`, `--quality`, `--flight`, `--walk`, ...).

**The eight-scene suite.** A scene is a fixed route driven by the autopilot through the real
physics, a fixed traffic seed with 60 s of warm-up, a frozen clock, a fixed weather preset and
one simulation step per drawn frame, so two runs differ only by noise:

```bash
scripts/benchmark_suite.sh --label "Debian 13 / Radeon 780M / opengles3"   # JSON into build/benchmarks
python3 scripts/benchmark_report.py build/benchmarks --label "..."         # Markdown tables
cp -r build/benchmarks build/bench-before                                  # keep a baseline, change, rerun
python3 scripts/benchmark_report.py build/benchmarks --against build/bench-before --label "after X"
```

Scenes: clear day, rain, clear night and rainy night, each from the chase and the cockpit
camera. `--route forest|country`, `--scenes "..."`, `--width/--height` and `--bin` vary it;
`--quick` (320 × 200, 240 frames) is for a software rasteriser or a smoke check. The suite
goes through `scripts/run_headless.sh`, which starts Xvfb when no `DISPLAY` is set.

**Hidden-GPU runs.** Three scripts drive the real GPU with SDL's `offscreen` video driver and
surfaceless EGL, so no window opens on anybody's desktop:

- `scripts/benchmark_gpu_scenes.sh` -- eight fixed scenes (town chase and cockpit, forest,
  snowy forest, fog, square with pedestrians, walking, helicopter), output in
  `build/benchmarks/p14-gpu-scenes/`;
- `scripts/benchmark_mirror_matrix.sh` -- rainy-night cockpit with mirror variants (`all`,
  `none`, `rear`, `rear192`, `rear_every2`, `rear75`, `unculled`, ...); list a variant twice to
  interleave repeats, e.g. `"none unculled all all unculled none"`;
- `scripts/capture_cockpit_weather.sh` -- seven cockpit time/weather captures for visual review.

Each writes JSON, a last-frame PNG, the log and the peak process RSS (sampled from
`/proc/<pid>/status` every 250 ms). Two limits are built in: SDL's offscreen EGL surface stays
at its initial **800 × 480**, so the scripts render at that size (a larger logical buffer is
silently clipped); and they **discard any run whose log does not report `driver radeonsi`**,
the reference machine's driver -- change that check for another GPU.

**Reading the counters.**

- `drawMsAvg` is CPU submission time inside `Draw`, not GPU execution. `frameMsAvg` adds
  scheduling and presentation. Vertical sync is on (`SynchronizeWithVerticalRetrace`), which is
  the likely reason frame wall times sit near 17–19 ms even when submission takes 4 ms; judge
  headroom from the draw and update timers.
- `drawCallsAvg`/`trianglesAvg` are the main view only (world, traffic, player car); they omit
  mirrors, pedestrians, signals and weather. `mesh3dAvg` counts every project-owned indexed 3D
  draw including mirrors, with the mirror share in `mirrorMesh3dAvg`. `pedestriansAvg` holds
  people alive and drawn with their submissions. SpriteBatch UI is in none of them.
- A desktop compositor throttles an unfocused window to about one present per second: wall
  times of ~1 s per frame in such runs are not GPU measurements. Use the hidden-GPU scripts.
- Host load moves timings by several milliseconds between runs. For an A/B, interleave the
  runs (`none, A, B, B, A, none`), compare the directly timed pass, and do not quote
  whole-frame percentages that the spread does not support.

## Current numbers

Reference machine: Debian 13, AMD Radeon 780M (RDNA 3 iGPU), Mesa 25.0.7 `radeonsi`, CNA
OPENGLES3, `high` tier, audio off. Measured during September 2026; later visual passes changed
triangle counts in the affected views by up to a few per cent, so re-run before relying on a
figure.

Eight-scene suite on the town route, 1280 × 720, window on the desktop display, 210 measured
frames:

| Scene | Draw submission | Frame wall | Main-view draws / triangles | World / traffic / mirror pass |
| --- | ---: | ---: | ---: | ---: |
| Clear day, chase | 10.5 ms | 17.6 ms | 1345 / 1.41 M | 6.4 / 3.0 / -- ms |
| Clear day, cockpit | 16.3 ms | 19.4 ms | 1376 / 1.41 M | 6.8 / 3.1 / 5.3 ms |
| Rain, chase | 8.9 ms | 17.5 ms | 1082 / 1.85 M | 4.5 / 3.0 / -- ms |
| Rain, cockpit | 14.2 ms | 18.1 ms | 1097 / 1.85 M | 4.4 / 3.1 / 5.4 ms |
| Clear night, chase | 13.3 ms | throttled | 1273 / 1.59 M | 6.7 / 3.4 / -- ms |
| Clear night, cockpit | 15.9 ms | throttled | 1293 / 1.59 M | 5.6 / 2.8 / 4.7 ms |
| Rainy night, chase | 14.3 ms | throttled | 1153 / 1.82 M | 5.8 / 4.2 / -- ms |
| Rainy night, cockpit | 16.6 ms | 19.8 ms | 1166 / 1.82 M | 4.8 / 3.1 / 5.3 ms |

"Throttled" rows lost focus and have no valid wall time; a focused night repeat is still to do.

Hidden-GPU fixed scenes (`benchmark_gpu_scenes.sh`), 800 × 480, 90 measured frames:

| Scene | Draw submission | Frame wall | World / traffic pass | Main-view draws / triangles | Peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Clear town, chase | 11.8 ms | 18.6 ms | 6.7 / 4.0 ms | 1324 / 1.45 M | 2197 MiB |
| Clear town, cockpit | 19.4 ms | 23.3 ms | 7.5 / 4.4 ms | 1349 / 1.45 M | 2194 MiB |
| Forest roadside | 4.2 ms | 18.7 ms | 2.5 / 0.6 ms | 515 / 0.71 M | 2184 MiB |
| Snowy forest roadside | 5.9 ms | 18.8 ms | 3.8 / 0.7 ms | 683 / 0.61 M | 2193 MiB |
| Fog, town square | 7.6 ms | 19.0 ms | 3.0 / 3.5 ms | 906 / 0.88 M | 2184 MiB |
| Square with pedestrians | 11.7 ms | 19.4 ms | 6.0 / 4.5 ms | 1291 / 1.22 M | 2193 MiB |
| Walking | 7.6 ms | 18.4 ms | 3.9 / 2.8 ms | 927 / 0.86 M | 2185 MiB |
| Helicopter over town | 20.4 ms | 21.8 ms | 11.7 / 7.6 ms | 1294 / 1.42 M | 2184 MiB |

The aerial view is the most expensive case (413 object and 40 tree batches visible). The cockpit
mirror pass is the largest single cost of the cockpit view.

**The mirrors** (rainy-night cockpit, interleaved runs): without mirrors 12.9 ms draw
submission, with all mirrors 18.5 ms, of which the directly timed mirror pass is about 6.1 ms.
The rear mirror alone costs 3.6 ms, and 1.6 ms when redrawn every second frame; a narrower
target (192 px) or a shorter distance (75 m) saved little. Culling a wing mirror whose glass is
outside the cockpit view removes 162 indexed 3D draws (8.4 % of the scene) and 388,000
triangles (9.6 %) a frame with an identical image; the timing spread does not support a
whole-frame speed-up figure.

**Update half** (Radeon, rainy-night cockpit with traffic, 36 pedestrians and audio on):
0.95 ms on average and 1.35 ms at worst -- vehicle physics 0.29 ms, traffic AI 0.23 ms,
collision 0.03 ms, audio mixer 0.32 ms. Older runs averaging several milliseconds were
inflated by the shadow re-bake hitches described under Levers.

**Software rendering**, for expectations only: under Xvfb with Mesa llvmpipe (four threads,
1280 × 720) the town chase frame submits in about 170 ms, and CNA's SOFTWARE renderer takes
3.6–3.8 s per frame. Load to the first frame is about 7 s there.

## Levers in use

- **Terrain LOD**: chunk steps 1/2/4 at 420 m / 1000 m, culled at 2300 m.
- **Object culling**: no object batch beyond the weather's `fogEnd`; building detail batches
  (frames, gutters, metal, reveals) beyond 420 m, tree trunks beyond 700 m, tree cards beyond
  1100 m. In dense fog tree cards and trunk batches follow the visibility horizon.
- **Traffic LOD**: full car within 45 m, no small parts to 130 m, reduced body to 900 m,
  nothing beyond; ground shadows within 120 m; frustum culling by bounding sphere. Parked cars
  drop detail sooner (25 / 70 / 400 m, shadows within 60 m).
- **Mirror**: world capped at 300 m (320 m far plane), update interval (`--mirror-every`,
  `settings.mirrorUpdateEvery`, default every frame), wing mirrors skipped when their glass is
  outside the view (`--no-wing-visibility-cull` replays the old behaviour for A/B runs).
- **Baked lighting**: building and tree shadows live in the terrain macro texture and road
  vertex colours. They are re-baked on a worker thread (about 0.8 s) when the sun has turned
  10 degrees -- only while the sun is above the horizon -- and the mip chain is prepared on
  the worker. The swap is still the worst update hitch: about 36 ms maximum in a matched
  daylight run (down from 151 ms). The vehicle shadow is a single draped draw.
- **Weather passes** run only when they have something to draw: the wet-road sheen is one extra
  submission per road batch (77 on the town route) while the road is wet, nothing when dry.
- **Quality tiers** (`--quality`, `settings.graphicsQuality`); nothing near the car changes:

| Tier | Draw distance | Vegetation | Mirror distance | Mirror rate |
| --- | ---: | ---: | ---: | ---: |
| low | × 0.50 | × 0.50 | 110 m | every 3rd frame |
| medium | × 0.75 | × 0.75 | 200 m | every 2nd frame |
| high (default) | × 1.00 | × 1.00 | 300 m | every frame |

On llvmpipe almost all of the tiers' saving is the mirror (the rasteriser is fill-bound); the
tiers have not yet been measured on a GPU, where fewer draws should matter more.

## Memory

Peak process RSS is about 2.2 GiB at the `high` tier. Known deliberate costs:

- the snow overlay keeps a compact `VertexPositionTexture` copy of every terrain chunk's three
  LODs (2.77 M vertices, **52.9 MiB**), allocated at start-up even in clear weather. It exists
  because Vulkan rejected the 40-byte dual-UV terrain layout in the snow pass; memory pressure
  measured on the Radeon did not justify replacing it;
- summer and winter tree atlases stay resident on the CPU (**17.5 MiB**); a full snow-cover
  change uploads about 11.7 MiB, gated to cover steps of about 0.04. The upload hitch during a
  live weather change has not been timed;
- two crown silhouettes per tree species add about 6.3 MiB of atlas with mips.

## Levers not taken

Measured costs have not yet justified any of these; see `ROADMAP.md`.

1. Merge building and prop batches per chunk across materials with a texture atlas.
2. Instanced trees (`DrawInstancedPrimitives` with the `BlendWeight` instance stream) and a
   single-quad far LOD.
3. One road-strip buffer per surface material instead of per piece.
4. A lower-resolution or lower-rate mirror by default (the measured saving was small).
