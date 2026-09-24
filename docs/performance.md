# Performance notes

Measured with `--benchmark --frames 400 --auto-drive 8` (statistics start after 30 warm-up
frames) on the development container: Xvfb + Mesa llvmpipe **software** OpenGL ES 3.2, four
CPU threads, 1280 x 720, sample map "Lipová", spawn "square", chase camera, traffic enabled.
Software rasterisation dominates these historical numbers. The measured Radeon 780M results
for Phase 14 are at the end of this document; its project draw submission is 9–17 ms in the
eight town scenes, so the old expectation of just a few milliseconds was optimistic.

| Build state | update avg | draw submission avg | frame wall avg | draw calls | triangles |
| --- | --- | --- | --- | --- | --- |
| Before terrain LOD (all chunks full resolution) | 0.48 ms | 93.6 ms | 232.6 ms | 615 | 828k |
| Terrain LOD (steps 1/2/4 at 420 m / 1000 m, cull 2300 m) | 0.43 ms | 47.0 ms | 133.1 ms | 541 | 231k |

Load time on the same machine: map data 1.4 s (terrain conformance dominates), world
geometry 5.4 s (three terrain LODs over 1920 chunks, 54k tree cards, 624 buildings), total
about 7 s to the first frame on a 6.4 x 7.6 km map.

## Measurements (per pass, LOD and culling)

Measured with `--benchmark --lockstep --frames 150 --auto-drive 6` (120 measured frames after
30 warm-up frames; `--traffic-warmup 40` on the town runs) on the same container. `--benchmark`
prints the table below and `--benchmark-json <file>` writes it as JSON; the same counters are in
the debug overlay (`F3`). Draw submission is the CPU time to submit the frame; the wall-clock
average includes llvmpipe's rasterisation and the swap.

Measured at the end of **Phase 12** and kept for the per-scene shape; the Phase 13 section below
re-measures the town scene and the mirror on the current build. Nothing in Phase 13 changed these
by more than a few per cent: the wet-road sheen runs only while the road is wet, and the paint's
cube map is rebuilt only when the sun has moved three degrees.

| Scene | draw submission | draw calls | triangles | cluster | mirror | sky | world | traffic | vehicle | hud |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Lipová chase (`--spawn square`, 20 traffic cars) | 170.4 ms | 1290 | 1242k | 0.4 | 0 | 1.2 | 106.9 | 56.5 | 5.2 | 0.2 |
| Nové Město (`--spawn mesto`) | 103.8 ms | 718 | 417k | 0.4 | 0 | 1.7 | 33.8 | 62.4 | 5.3 | 0.2 |
| Podhájí (`--spawn podhaji`) | 74.7 ms | 615 | 492k | 0.4 | 0 | 1.9 | 38.5 | 27.7 | 6.0 | 0.2 |
| Forest road (`--spawn forest`) | 56.7 ms | 469 | 532k | 0.4 | 0 | 1.9 | 42.2 | 6.1 | 6.0 | 0.2 |
| Fields (`--spawn fields`) | 46.8 ms | 493 | 547k | 0.4 | 0 | 1.7 | 37.8 | 0 | 6.3 | 0.6 |

Pass columns are milliseconds per frame, at 13:00 with the clock frozen and the default
scattered-cloud weather. Visible batches on the Lipová chase run: 204 terrain chunks, 77 road
batches, 456 object batches, 33 tree batches; of the 20 traffic cars 8 are drawn per frame on
average and of the 150 parked cars 12 (the debug overlay and the benchmark JSON count them
separately). The Nové Město run draws 77 terrain chunks and 121 object batches, the forest run
80 tree batches and 78 object batches, the fields run 202 terrain chunks and 154 object batches.

The world roughly doubled in area with Phase 12 (6.4 x 7.6 km against 4.2 x 5.8 km, four more
settlements, 624 buildings against 370). The terrain grid went from 4 m to 5 m to pay for it, so
the chunk count only rose from 1518 to 1920 and the *visible* terrain chunks actually fell (204
against 296); the extra frame time is objects, not ground -- from Lipová the neighbouring
villages now stand on the horizon, and the object batches rose from 280 to 456. Night and rain
each add about 2 ms and 0.4 ms respectively; the lamp and rain passes are skipped outright when
they have nothing to draw.

### Levers in use

- **Terrain LOD**: chunk steps 1/2/4 at 420 m / 1000 m, cull at 2300 m (M10).
- **Object fog cull**: no object batch is submitted beyond the rig's `fogEnd`, where it
  would be indistinguishable from the fog itself; detail batches keep their shorter 420 m
  range. Worth about 7 ms of the Lipová frame now that other settlements are in view.
- **Object detail cull**: frame, gutter, metal and reveal batches of buildings are skipped
  beyond 420 m (`ObjectBatch::cullDistance`); tree trunks beyond 700 m; tree cards beyond
  1100 m.
- **Traffic LOD** (RQ-031): LOD 0 (full car) within 45 m, LOD 1 (no small parts) to 130 m,
  LOD 2 (reduced body, no glass, lamp glows or plate) to 900 m, nothing further; ground
  shadows only within 120 m. Cars outside the frustum are skipped by their bounding sphere.
- **Parked cars** are scenery, so they drop detail sooner: LOD 0 within 25 m, LOD 1 to 70 m,
  LOD 2 to 400 m, shadows within 60 m. The cars visible from the town road cost about 14 ms of
  the traffic pass; with the traffic radii they would cost roughly twice that.
- **Mirror update interval** (`mirrorUpdateEvery` in the save file, `--mirror-every <n>`):
  the mirror target is redrawn every n frames and the previous image is shown in between.
  Every second frame halves the mirror cost (61.6 to 30.7 ms here, 29 ms per frame overall);
  on a real GPU the saving is a few percent, so the default stays 1.
- **Vehicle ground shadow** is a convex-hull blob draped on the sampled ground (one draw call)
  instead of a stencil volume; building and tree shadows are baked into the terrain macro
  texture and the road vertex colours at load time (`GroundShadowBaker`), so they cost no
  per-frame time.

## Where the time goes

- `Update` (physics at 120 Hz sub-steps, traffic, collision, audio mixing) stays below 0.5 ms.
- Draw submission is dominated by the number of batches: roads (71), buildings/props in 256 m
  chunks (up to ~180 visible), tree batches per chunk and species, traffic cars (about 25
  parts each). Frustum culling and the tree distance cut (1100 m) are the main reducers.
- The cockpit view adds the rear-view mirror pass (sky + world + vehicles again at 768 x 200)
  and the instrument cluster (SpriteBatch into a 1024 x 448 target).

## Levers that remain (PERF-002)

1. Merge building/prop batches per chunk across materials with a small texture atlas
   (would cut object draw calls roughly by four).
2. Instanced trees (`DrawInstancedPrimitives` with the BlendWeight instance stream) instead of
   per-chunk static meshes; a single-quad far LOD beyond 500 m.
3. Mirror pass at lower resolution (the update interval exists, see above).
4. Road strips: one buffer per surface material instead of per piece.

## Phase 13: the benchmark suite, the mirror, and the quality tiers

### How to reproduce any number on this page

Everything below comes from the deterministic scenarios, not from a hand-driven session:

```bash
scripts/benchmark_suite.sh --label "<what machine, what renderer>"     # eight scenes
python3 scripts/benchmark_report.py build/benchmarks --label "..."     # the tables
python3 scripts/benchmark_report.py build/benchmarks --against build/bench-before   # a comparison
```

A scene is a fixed route driven by the autopilot over the real physics (`--route town`), a fixed
traffic seed with a 60 s warm-up, a frozen clock and a fixed weather preset, one simulation step
per drawn frame. Two runs on the same machine differ only by noise. `--quick` (320 x 200,
240 frames) is what the numbers in this section were taken at, because a software rasteriser
cannot do eight scenes at 1280 x 720 in a sensible time.

**Every table on this page is the container's software rasteriser (Mesa llvmpipe, four CPU
threads, no GPU).** They are an upper bound dominated by fill rate and say very little about a
real machine. `docs/real-hardware-validation.md` is the procedure for reproducing hardware
results. Phase 14's first real GPU run is recorded below.

### The rear-view mirror

Profiled before anything was changed, on the `town` route in the cockpit at 320 x 200, 150
frames:

| | mirror pass | frame wall | draw calls | triangles |
| --- | ---: | ---: | ---: | ---: |
| before (mirror far plane 1500 m, no distance cap) | 58.9 ms | 254.5 ms | 1330 | 1.25 M |
| after (far plane 320 m, world capped at 300 m) | 45.5 ms | 244.6 ms | 1330 | 1.25 M |

A quarter of the cockpit frame was a second full world pass drawn to a strip 200 pixels tall at
eleven degrees of vertical field, where nothing past a couple of hundred metres can be made out.
The mirror is not removed and its resolution is unchanged; the image is indistinguishable.

### The quality tiers

Same scene, same frames, `--quality low|medium|high`:

| tier | frame wall | worst 1 % | mirror | world | terrain chunks | object batches | tree batches |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| high (default) | 240.4 ms | 282.5 ms | 45.5 ms | 100.1 ms | 203 | 495 | 33 |
| medium | 221.4 ms | 275.9 ms | 23.0 ms | 99.3 ms | 124 | 427 | 27 |
| low | 204.9 ms | 260.5 ms | 13.8 ms | 85.0 ms | 63 | 276 | 12 |

Read this honestly: on a software rasteriser almost all of the saving is the mirror, because
llvmpipe is fill-bound and a distant terrain chunk covers very few pixels — halving the visible
chunk count barely moves the world pass. On a GPU the same change removes draw calls and vertex
work, which is where a GPU frame goes, so the tiers should help more there and that is one of the
things the real-hardware run is for. The tiers are:

| | draw distance | vegetation | mirror distance | mirror rate |
| --- | ---: | ---: | ---: | ---: |
| low | x0.50 | x0.50 | 110 m | every 3rd frame |
| medium | x0.75 | x0.75 | 200 m | every 2nd frame |
| high | x1.00 | x1.00 | 300 m | every frame |

Nothing within close range of the car changes at any tier: the car, the cockpit, the road under
the wheels and the traffic beside you are the point of the project and are not traded away for a
frame time. The tier lives in the save file as `settings.graphicsQuality` and `--quality`
overrides it for one run; the `F3` overlay reports which one is in force.

### Where the update time goes

The `--benchmark` output and the overlay now split the update half the same way. On the `town`
route with twenty traffic cars:

| | vehicle physics | collision | traffic AI | audio |
| --- | ---: | ---: | ---: | ---: |
| clear day, exterior | 0.185 ms | 0.006 ms | 0.182 ms | 0.001 ms |
| clear day, cockpit | 0.183 ms | 0.006 ms | 0.182 ms | 0.001 ms |

The whole update is under half a millisecond and the camera does not touch it. Nothing in the
simulation is a bottleneck at this world size; every lever that matters is in the draw half.

### The wet-road sheen

The extra pass costs one more submission of each non-marking road batch while the road is wet
(77 batches on the town route, about 1.5 ms on llvmpipe) and nothing at all when it is dry.

## Phase 14: first real GPU baseline

Measured at commit `2fa9ddd` on Debian 13, AMD Radeon 780M (`radeonsi`, Mesa 25.0.7), CNA
OPENGLES3 / OpenGL ES 3.2, actual desktop display `:0`, 1280 × 720, high quality. The command
was `DISPLAY=:0 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy scripts/benchmark_suite.sh
--out build/benchmarks/p14-gpu-baseline --frames 240`. Each deterministic town-route scene has
30 warm-up frames and 210 measured frames, 60 simulated seconds of traffic warm-up and about
20 traffic cars. Audio was disabled in this controlled graphics run.

| Town route | Project draw submission | Frame wall clock | Instrumented draws | Instrumented triangles | World pass | Traffic pass | Mirror pass |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Clear day, exterior | 10.53 ms | 17.59 ms | 1345 | 1.41 M | 6.44 ms | 3.03 ms | 0 |
| Clear day, cockpit | 16.26 ms | 19.39 ms | 1376 | 1.41 M | 6.78 ms | 3.09 ms | 5.33 ms |
| Rain, exterior | 8.86 ms | 17.49 ms | 1082 | 1.85 M | 4.50 ms | 3.03 ms | 0 |
| Rain, cockpit | 14.18 ms | 18.08 ms | 1097 | 1.85 M | 4.44 ms | 3.05 ms | 5.41 ms |
| Clear night, exterior | 13.32 ms | unreliable | 1273 | 1.59 M | 6.73 ms | 3.41 ms | 0 |
| Clear night, cockpit | 15.88 ms | unreliable | 1293 | 1.59 M | 5.56 ms | 2.83 ms | 4.70 ms |
| Rainy night, exterior | 14.30 ms | unreliable | 1153 | 1.82 M | 5.76 ms | 4.18 ms | 0 |
| Rainy night, cockpit | 16.59 ms | 19.81 ms | 1166 | 1.82 M | 4.79 ms | 3.08 ms | 5.32 ms |

The desktop intermittently throttled an unfocused simulator window to roughly one present per
second: wall-clock averages reached 537 ms in clear night exterior and 1022 ms in rainy night
exterior even though their project draw submission stayed at 13–14 ms. Those wall times are
**not GPU performance measurements**. The other wall times were stable at 17–20 ms, but a
foreground, unthrottled repeat is required before quoting a reliable night FPS. Project
timers measure CPU submission; they do not isolate GPU execution or present/compositor time.

The existing `drawCallsAvg` and `trianglesAvg` counters include the main-view world, traffic
and player vehicle instrumentation. They omit some submissions, including pedestrians,
signals, weather and mirror re-draws, so they are not whole-frame GPU draw counts. They are
useful for same-scene changes to those instrumented systems, not a global submission budget.
In clear day, the main-view world pass (6.4–6.8 ms), traffic pass (~3.0 ms) and cockpit mirror
(5.3 ms) dominate the project draw timer. No Phase 14 batching has been performed from these
numbers alone. Forest, snow, fog, walking, aerial and pedestrian-heavy cases remain to be
measured before a global draw-call decision.

### Phase 14 supplemental views and counter coverage

The following fixed-camera runs use the same Radeon 780M, OPENGLES3, 1280 × 720 and high
quality. They are shorter (30–60 measured frames after 30 warm-up frames) than the eight-scene
suite and were used to locate visual/performance risks, not to claim a stable FPS ranking.
The desktop sometimes throttled the unfocused window to one present per second. Project draw
submission is CPU time inside `Draw`, not a GPU execution timer.

| View | State | Project draw submission | Main-view instrumented draws / triangles | Extra evidence |
| --- | --- | ---: | ---: | --- |
| Forest roadside | clear | 5.84 ms | 485 / 533k | 80 visible tree batches, ~7 traffic cars; wall time throttled |
| Forest roadside | snow | 6.30 ms | 703 / 531k | 80 tree batches; 17.09 ms wall average in this run |
| Town fixed view | dense fog, before tree cull | 7.06 ms | 849 / 833k | 33 tree batches; white distant foliage in screenshot |
| Town fixed view | dense fog, after tree cull | 12.24 ms | 822 / 829k | 6 tree batches; CPU timing varied, so no timing win is claimed |
| Square pedestrian view | old people | 9.52 ms | not retained | 12 people drawn, 105 pedestrian submissions / 14.9k triangles |
| Square pedestrian view | rounded people | 12.50–12.81 ms | not retained | 12 people drawn, 129 pedestrian submissions / 18.9k triangles; other passes varied |
| Square walking camera | clear | 8.50 ms | 883 / 859k | 36 people alive, 4 drawn, 42 pedestrian submissions |
| Helicopter aerial camera | clear | 19.26 ms | 1424 / 1.28M | World 9.84 ms, traffic 8.57 ms, 26 parked cars visible |

The main-view draw and triangle columns still exclude pedestrians. A separate
`pedestriansAvg` object in current benchmark JSON records alive/drawn people plus their
main-view submissions and triangles. Mirror, signal and weather submissions are also not
included in `drawCallsAvg`. The pedestrian comparison shows the geometry cost of a nicer
silhouette; the project draw timer varied with world/traffic work and window scheduling, so
the table does not assign its whole 3 ms difference to the pedestrian change. Aerial viewing
is the highest measured project submission case here, with both world and traffic needing
further investigation before batching decisions.
