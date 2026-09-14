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

Load time on the same machine: map data 0.5 s (terrain conformance dominates), world
geometry 3.1 s (three terrain LODs, 44k tree cards, 356 buildings), total about 4 s to the
first frame.

## Phase 11 measurements (per pass, LOD and culling)

Measured with `--benchmark --lockstep --frames 150 --auto-drive 6` (120 measured frames after
30 warm-up frames; `--traffic-warmup 40` on the town runs) on the same container. `--benchmark`
prints the table below and `--benchmark-json <file>` writes it as JSON; the same counters are in
the debug overlay (`F3`). Draw submission is the CPU time to submit the frame; the wall-clock
average includes llvmpipe's rasterisation and the swap.

| Scene | draw submission | draw calls | triangles | cluster | mirror | sky | world | traffic | vehicle | hud |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Town chase (`--spawn square`, 20 traffic cars) | 84.7 ms | 705 | 575k | 0.5 | 0 | 1.6 | 46.3 | 30.5 | 5.5 | 0.3 |
| Town cockpit, mirror every frame | 144.8 ms | 785 | 832k | 0.5 | 62.8 | 0.5 | 64.1 | 12.5 | 4.1 | 0.2 |
| Town cockpit, `--mirror-every 2` | 124.3 ms | 785 | 832k | 0.5 | 31.8 | 1.1 | 72.4 | 14.0 | 4.2 | 0.2 |
| Forest road (`--spawn forest`) | 65.6 ms | 490 | 812k | 0.5 | 0 | 1.7 | 56.3 | 0 | 6.8 | 0.3 |
| Fields (`--spawn fields`) | 19.3 ms | 211 | 196k | 0.5 | 0 | 1.5 | 10.9 | 0.8 | 5.4 | 0.2 |

Pass columns are milliseconds per frame. Visible batches on the town chase run: 257 terrain
chunks, 25 road batches, 177 object batches, 7 tree batches; of the 20 traffic cars 7 are drawn
per frame on average (2.5 at LOD 0, 1.5 at LOD 1, 3 at LOD 2). The forest run draws 89 tree
batches and 90 object batches; the fields run 126 terrain chunks and 25 object batches.

The Phase 11 building detail (window frames, gutters, reveals, plots, poles) raised the town
triangle count from about 341k to 558k before culling; the levers below bring the measured
frames back to the numbers in the table.

### Levers in use

- **Terrain LOD**: chunk steps 1/2/4 at 420 m / 1000 m, cull at 2300 m (M10).
- **Object detail cull**: frame, gutter, metal and reveal batches of buildings are skipped
  beyond 420 m (`ObjectBatch::cullDistance`); tree trunks beyond 700 m; tree cards beyond
  1100 m.
- **Traffic LOD** (RQ-031): LOD 0 (full car) within 45 m, LOD 1 (no small parts) to 130 m,
  LOD 2 (reduced body, no glass, lamp glows or plate) to 900 m, nothing further; ground
  shadows only within 120 m. Cars outside the frustum are skipped by their bounding sphere.
- **Mirror update interval** (`mirrorUpdateEvery` in the save file, `--mirror-every <n>`):
  the mirror target is redrawn every n frames and the previous image is shown in between.
  Every second frame halves the mirror cost (62.8 to 31.8 ms here, 20 ms per frame overall);
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
