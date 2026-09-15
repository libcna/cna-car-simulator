# Performance notes

Measured with `--benchmark --frames 400 --auto-drive 8` (statistics start after 30 warm-up
frames) on the development container: Xvfb + Mesa llvmpipe **software** OpenGL ES 3.2, four
CPU threads, 1280 x 720, sample map "Lipová", spawn "square", chase camera, traffic enabled.
Software rasterisation dominates these numbers; a discrete or integrated GPU renders the same
frame in a few milliseconds, so the wall-clock figures below are an upper bound that mainly
tracks triangle and draw-call counts.

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
