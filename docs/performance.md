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

### Phase 14 rainy-night combined graphics checkpoint

At commit `5a88b8b`, a deterministic town-route run combined 22:30, rain, cockpit, headlights,
high graphics, the live rear-view mirror, 60 simulated seconds of warmed traffic and the
pedestrian population. It ran on the Debian 13 desktop's AMD Radeon 780M with Mesa 25.0.7,
CNA OPENGLES3, 1280 × 720. Audio was disabled to isolate graphics. The command used
`--no-save --no-audio --lockstep --route town --route-stay --frames 150 --traffic-warmup 60
--time 22:30 --time-scale 0 --weather rain --lights --cockpit --width 1280 --height 720
--quality high --benchmark`. The retained machine-readable result is
[p14-rainynight-cockpit-5a88b8b.json](performance-data/p14-rainynight-cockpit-5a88b8b.json).

Across 120 measured frames after 30 warm-up frames, project draw submission averaged
**16.06 ms** (18.68 ms maximum), while update averaged **0.537 ms**. The mirror pass was
**6.58 ms**, about 41% of the project draw timer; world was 4.48 ms, traffic 2.51 ms and
player vehicle 2.03 ms. The instrumentation reported 1,135 main-view draws and 1.845 M
triangles on average, 20 traffic cars (8.1 drawn plus 13 parked), and 36 pedestrians alive
(7 drawn). The `drawCallsAvg` counter still omits mirror and pedestrian submissions, so it is
not a whole-frame draw count. The desktop again held the window near one present per second
(1026 ms reported average wall frame), even after an attempted focus change. That wall figure
cannot be used as GPU FPS. This combined scene identifies the mirror as the largest measured
project submission pass; it does not prove that draw calls or GPU execution are the bottleneck,
so no batching change follows from this single run.

### Phase 14 urban asphalt repair cost

The deterministic repair cuts added to urban asphalt reuse each piece's existing road batch;
they do not create a draw submission or alter collision/lane geometry. A fixed Radeon 780M,
OPENGLES3, 1280 × 720 town scene (`--view -78 9 -30 50 -6`, 13:00 cloudy, 40 frames with
30 benchmark warmup frames) was repeated against the pre-repair `f61e7ed` capture:

| Scene | Instrumented draws | Triangles | Project draw submission |
| --- | ---: | ---: | ---: |
| Before repair cuts | 1215 | 1,450,306 | 9.46 ms |
| After repair cuts | 1215 | 1,450,906 | 11.10 ms |

The change is **0 draws** and **+600 triangles** (+0.041%). The 1.64 ms draw-timer difference
is not assigned to the repair geometry: these ten-frame desktop captures are affected by
unfocused-window throttling and natural pass-time variation. The offscreen map geometry test
found repairs in 20 asphalt pieces. Their coarse snow-only base surfaces add 43,232 vertices
compared with 599,752 detailed paved vertices across the full map (7.2%); retaining a full
duplicate of each repaired piece would have added 254,392 vertices (42.4%). The lower-detail
surface changed the fixed snow capture by less than 0.001 mean RGB level per channel while
preventing the overlapping repair quad from receiving snow twice. Rain continues to use the
detailed road surface. [Before/after and weather captures](screenshots/phase14/README.md)
document the visible result.

### Phase 14 church frontage geometry

A matched 40-frame Radeon 780M OPENGLES3 capture at the square (1280 × 720, 13:00,
scattered cloud, fixed `--view -78 9 -30 50 -6`, 20-second traffic warmup) compared the
church tower before and after adding pilasters, an oculus and a shallow entrance portal.
Both captures made **1215 instrumented draws**. Reported triangles rose from **1,450.91k**
to **1,451.45k** (about 540, under 0.04%); the facade joins existing object material
batches. Project draw submission was 11.78 versus 12.00 ms across ten measured frames. The
desktop throttled both unfocused runs to about 1.13 s wall time per frame, and this small
CPU timer difference cannot be attributed to the facade. The [paired screenshots](screenshots/phase14/README.md)
show the visible change; the large paved square and surrounding facades still need work.

### Phase 14 forest silhouette atlas cost

Two deterministic silhouette variants now share each species' tree-card atlas, so tree seed
changes crown shape without multiplying tree batches. A fixed Radeon 780M OPENGLES3 forest
edge (`--spawn forest --view -228 5 -1280 0 -4`, clear 13:00, 1280 × 720, 40 frames,
20-second traffic warmup) retained **443 instrumented draws, 720,585 triangles and 88 visible
tree batches** before and after. Project draw submission was 5.77 versus 4.96 ms over ten
measured frames; the desktop throttled both unfocused windows to about 1.12 s/frame, so
this is not evidence of a speedup. The source RGBA8 cards grow from 4.00 to 8.75 MiB for
eight species, an estimated 6.33 MiB increase including full mip chains. One-off world
construction was 4.02 versus 4.48 s, too few runs to attribute the difference. The paired
[forest frames](screenshots/phase14/README.md) show varied crowns at the same camera;
the original snow check has no atlas seam but exposed the need for canopy snow treatment.

### Phase 14 seasonal tree-atlas memory cost

The later snow-crown pass keeps summer and winter RGBA8 atlases for all eight species on the
CPU (2 × 8 × 560 × 512 × 4 bytes = **17.5 MiB** of resident source images). A blended atlas
is temporary during each seasonal update. The existing eight GPU textures are updated in
place through `Texture2D::SetData`, including their mip levels; there is no second GPU atlas,
tree mesh or tree draw pass. One full-cover update uploads about **11.7 MiB** across all
eight mip chains. During gradual snowfall/melt, uploads are gated to roughly 0.04 cover
increments. A fixed 1280 × 720 forest capture changed crown colour but retained geometry
and culling; the clear capture is byte-identical to the pre-change frame. The fixed snowy
[before/after images](screenshots/phase14/README.md) show the visual gain. Upload hitches
during live weather transitions have not yet been timed on the Radeon and remain an explicit
performance check before this treatment is considered finished.

### Phase 14 snow terrain vertex-layout cost

The snow overlay now uploads a second, compact `PositionTexture` vertex buffer
for each of the 1,920 terrain chunks' three LODs. The snow and ordinary terrain
meshes share index buffers, and the existing single snow draw per visible chunk
is unchanged. From the map's 6,400 × 7,600 m size and 5 m terrain cell, the
three LODs total 2,773,280 snow vertices × 20 bytes = **52.9 MiB** of extra
GPU vertex data. This is allocated at world construction even in clear weather.
The cost is accepted for now because it restores Vulkan snow rendering and
preserves the existing OPENGLES3 and SOFTWARE images byte-for-byte. It is a
memory trade-off for renderer conformance, not a draw-call optimization; memory
pressure and construction cost still need real-hardware measurement.

### Phase 14 hidden Radeon setup correction

SDL `offscreen` with surfaceless EGL reaches the Radeon 780M without a desktop window:
Mesa reports PCI `1002:15bf` and `driver radeonsi`. An initial trial requested a 1280 × 720
logical buffer, but inspection of every captured PNG found non-black pixels only in
`(0, 240)–(800, 720)`. The SDL offscreen EGL surface had remained at its initial 800 × 480
physical size. The trial's timings and images were discarded: they are neither a valid
1280 × 720 performance run nor a visual comparison. The two benchmark scripts now request
800 × 480, matching that physical surface, and check the driver and reported size.
A full 800 × 480 test frame was visually inspected and fills the image. This is an explicit
limitation of the hidden Radeon setup, not a reason to alter CNA or to touch the public
XNA-only boundary. The desktop `:0` is no longer used for automated runs because it
interferes with the person's screen.

### Phase 14 hidden Radeon 800 × 480 measurements

Both scripts run with `DISPLAY` and `WAYLAND_DISPLAY` unset, `SDL_VIDEODRIVER=offscreen`,
`EGL_PLATFORM=surfaceless`, and Mesa's Radeon `radeonsi` driver. They use high quality,
fixed simulation time, 60 seconds of traffic warm-up, 30 frame warm-up, 90 measured frames,
and no audio. The captured images were inspected for full-frame rendering. This setup has
no desktop compositor, but `drawMsAvg` remains a CPU submission timer and `frameMsAvg`
includes scheduling and presentation; neither is a pure GPU execution timer. Compare these
800 × 480 runs with each other, not the older desktop 1280 × 720 rows.

| Rainy night cockpit, mirror setting | Draw submission | Mirror pass | Frame wall | Main-view draws / triangles |
| --- | ---: | ---: | ---: | ---: |
| All mirrors, default rear 768 × 200 | 41.66 ms | 13.86 ms | 49.30 ms | 1127 / 1.845 M |
| No mirrors | 16.13 ms | 0 | 21.17 ms | 1127 / 1.845 M |
| Rear only, 768 × 200 | 21.23 ms | 4.33 ms | 25.68 ms | 1127 / 1.845 M |
| Rear only, 384 × 100 | 13.23 ms | 2.61 ms | 18.75 ms | 1127 / 1.845 M |
| Rear only, 192 × 50 | 13.39 ms | 2.64 ms | 18.74 ms | 1127 / 1.845 M |
| Rear only, every second frame | 12.25 ms | 1.39 ms | 18.71 ms | 1127 / 1.845 M |
| Rear only, 150 m distance | 11.54 ms | 2.27 ms | 18.66 ms | 1127 / 1.845 M |
| Rear only, 75 m distance | 12.09 ms | 2.25 ms | 18.54 ms | 1127 / 1.845 M |

The machine's concurrent load changed during the sequential run: even the main-view world
and traffic passes became faster later in the series. In a second sequence ordered
`none, all, rear, all, none`, the two all-mirror passes averaged 16.73 and 16.23 ms, and
the rear-only pass 4.65 ms. The first no-mirror draw averaged 20.84 ms; the last rose to
31.77 ms while the fixed main-view world pass rose from 8.26 to 12.56 ms. A subsequent
host-load surge drove the rear 384-pixel run to 110.86 ms draw submission and disqualifies
that run from comparisons. The direct mirror-pass readings establish substantial mirror
cost, but the whole-frame differences are **not** valid speed-up percentages. The rear pass
fell from 4.33 ms at its default target to about
2.6 ms at both 384 and 192 pixels wide; further resolution reduction did not help in this
run. Every-second-frame rear updates averaged 1.39 ms. The 150 m and 75 m distance results
were similar, under the same changing host load. Main-view draw and triangle counters exclude
mirror work, explaining why those columns do not change.

| Fixed scene | Draw submission | Frame wall | World / traffic passes | Main-view draws / triangles | Peak process RSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Clear town, chase | 11.77 ms | 18.62 ms | 6.74 / 3.96 ms | 1324 / 1.446 M | 2197 MiB |
| Clear town, cockpit | 19.38 ms | 23.25 ms | 7.52 / 4.36 ms | 1349 / 1.446 M | 2194 MiB |
| Forest roadside | 4.19 ms | 18.65 ms | 2.54 / 0.55 ms | 515 / 0.706 M | 2184 MiB |
| Snow forest roadside | 5.86 ms | 18.76 ms | 3.79 / 0.70 ms | 683 / 0.605 M | 2193 MiB |
| Fog town square | 7.55 ms | 18.95 ms | 2.98 / 3.54 ms | 906 / 0.884 M | 2184 MiB |
| Square pedestrians | 11.65 ms | 19.40 ms | 5.98 / 4.48 ms | 1291 / 1.217 M | 2193 MiB |
| Walking | 7.63 ms | 18.37 ms | 3.85 / 2.80 ms | 927 / 0.860 M | 2185 MiB |
| Helicopter aerial over town | 20.39 ms | 21.79 ms | 11.71 / 7.60 ms | 1294 / 1.421 M | 2184 MiB |

The town cockpit mirror pass was 6.35 ms. The aerial view exposed the most world/object
batches (413 object, 40 tree), so its substantial submission time is scene-dependent, not
evidence for a general batching rewrite. Peak RSS was sampled from `/proc/<pid>/status` at
250 ms intervals; it is process memory, not dedicated GPU memory. On this host, 17 GiB of
system memory was available after the series. Snow forest peak RSS exceeded clear forest by
about 9 MiB, less than normal cross-run variation in the mirror matrix. This does not justify
replacing the working Vulkan-compatible snow layout. The [mirror JSON](performance-data/p14-mirror-offscreen-800-all.json),
[repeat JSON](performance-data/p14-mirror-bracket-800-all.json),
[scene JSON](performance-data/p14-scene-offscreen-800-town_clear.json), and
[scene captures](screenshots/phase14/README.md) are retained for review.

### Paired mirror check before P14-051 (2026-09-25)

At `88d57a9`, the same hidden Radeon 780M / Mesa 25.0.7 / OPENGLES3 setup ran the
rainy-night cockpit in the interleaved order `none, all, rear, rear192, rear_every2,
rear75, rear192, rear, all, none`. Each run used 120 lockstep frames (30 warm-up,
90 measured), 60 s traffic warm-up, high quality and the same 800 × 480 camera. The
script checked `driver radeonsi`, physical image size and scene after every run. A first
sandboxed attempt had no Radeon device and took 319 ms to submit a no-mirror frame; it
was discarded. The accepted runs had no desktop window or compositor.

| Mirror mode | Draw submission, ms | Mirror pass, ms | Frame wall, ms | Peak RSS, MiB |
| --- | ---: | ---: | ---: | ---: |
| None, first / last | 13.05 / 12.67 | 0 / 0 | 19.83 / 19.33 | 2200 / 2204 |
| All, first / repeat | 18.04 / 18.93 | 6.03 / 6.25 | 22.66 / 23.23 | 2188 / 2203 |
| Rear only, first / repeat | 16.79 / 17.80 | 3.56 / 3.68 | 21.24 / 22.35 | 2187 / 2186 |
| Rear 192 × 50, first / repeat | 15.03 / 17.45 | 3.17 / 3.54 | 19.60 / 21.82 | 2188 / 2188 |
| Rear every second frame | 13.68 | 1.56 | 19.69 | 2187 |
| Rear 75 m distance | 16.64 | 3.18 | 21.24 | 2194 |

All variants retained 1129 main-view draw submissions and about 1.847 M main-view
triangles; those counters exclude mirror passes. The mean of the two all-mirror draw
times is 18.49 ms versus 12.86 ms without mirrors, a **5.63 ms (44%)** increase
relative to no mirror in this paired scene. The directly timed mirror pass is about
6.14 ms, roughly one third of all-mirror draw submission. Dropping wing mirrors cuts
that pass to 3.62 ms. Reducing the rear target to 192 pixels or its distance to 75 m
saves little at this host load; every-second-frame updates cut the rear pass to 1.56 ms.
The 0.6–1.0 ms spread between repeated rear/all runs and very large single-frame update
spikes limit precise whole-frame speed-up claims. Frame wall is offscreen and unthrottled
by a compositor, but still includes scheduler/presentation time; draw/pass numbers are
project CPU submission timers, not isolated GPU execution. The paired JSON is in
[`performance-data/`](performance-data/) under `p14-mirror-paired-800-*`; representative
[all](screenshots/phase14/offscreen-800-mirror-paired-all.png),
[rear](screenshots/phase14/offscreen-800-mirror-paired-rear.png) and
[none](screenshots/phase14/offscreen-800-mirror-paired-none.png) captures fill 800 × 480.
The all/rear image difference is confined to the visible left wing mirror (522 pixels
with >5/255 per-channel change, bounding box x=134–194, y=281–308). The right wing
reflection did not contribute visible pixels in this fixed cockpit view even though both
wing views were rendered in alternating frames. This provides a measured, narrow P14-051
candidate: skip a wing pass when its glass is outside the main cockpit frustum.

Together with the eight fixed scenes and the earlier resolution, distance and rate
matrix above, this completes the controlled P14-050 Radeon baseline at 800 × 480. It
does not justify changing the Vulkan-compatible snow terrain layout: the clear/snow RSS
difference remains below normal run variation, and there is no measured memory pressure.

### Targeted P14-051 wing-mirror visibility pass

The first P14-051 change tests each wing glass against the current cockpit frustum before
rendering its mirror scene. In the standard rainy-night cockpit the left glass intersects
and the right does not. An out-of-view image is invalidated, so turning the camera toward
that glass redraws it at once. The rear-view mirror, target sizes, draw distances and
visible wing's alternating update rate are unchanged. A three-frame hidden Radeon
rightward look (`--eye 0 0 0 -35 0`) showed 483 pixels of passenger-side reflection
compared with `--no-wing-mirrors` (x=528–546, y=264–292). The standard all-mirror
800 × 480 result is pixel-identical before and after the cull.

| Rainy-night cockpit | Before cull (`b695e1a`) | After cull, comparable run | Difference |
| --- | ---: | ---: | ---: |
| Direct mirror pass | 6.028 ms | 4.931 ms | −1.097 ms (−18.2%) |
| Whole draw submission | 18.043 ms | 17.263 ms | −0.780 ms (−4.3%) |
| Frame wall | 22.658 ms | 21.837 ms | −0.821 ms (−3.6%) |
| Main-view submissions | 1129 | 1129 | 0 |
| Main-view triangles | 1,847,014 | 1,847,014 | 0 |
| Peak process RSS | 2188 MiB | 2185 MiB | −3 MiB, within run noise |

The comparable pair had near-matched main-view world / traffic / vehicle times:
4.885 / 3.341 / 3.342 ms before and 5.059 / 3.442 / 3.390 ms after. However a later
after-cull repeat saw those same passes rise to 6.926 / 4.896 / 4.675 ms and the mirror
pass to 6.603 ms; a no-mirror repeat also rose from 11.966 to 20.385 ms total draw.
Thus the direct mirror-pass reduction is supported by the comparable pair and by one
fewer offscreen wing scene every other frame, while the whole-frame percentages above
are observations, **not** a stable speed-up estimate. The existing main-view counters do
not include mirror submissions or triangles, so P14-051 remains open for a matched
total-submission accounting pass. No snow-memory optimisation was made.

The [before JSON](performance-data/p14-mirror-paired-800-all.json),
[after JSON](performance-data/p14-mirror-wing-cull-800-all.json),
[after repeats](performance-data/p14-mirror-wing-cull-800-all-r2.json),
[before image](screenshots/phase14/offscreen-800-mirror-paired-all.png) and
[after image](screenshots/phase14/offscreen-800-wing-cull-after.png) retain the comparison.
All four public-XNA renderer paths were then built and their cockpit captures inspected;
see [renderer conformance](renderer-conformance.md).
