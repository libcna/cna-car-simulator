# Renderer conformance

The simulator talks only to the XNA 4.0-compatible public API of CNA (`docs/api-boundary.md`,
enforced by `scripts/check_xna_only.py`), so the same binary source builds against every CNA
renderer. This page records the renderers that were built and run in the development
container and what differed between them. There is no renderer-specific code in the project;
the renderer is chosen with CNA's `CNA_GRAPHICS_RENDERER` cache variable through the CMake
presets (`opengles3`, `opengl33`, `software`, `vulkan`, `default`).

Phase 13 environment (historical results below): Linux container, Xvfb 1280 x 720, Mesa llvmpipe (software OpenGL, 4 threads),
no GPU, no audio device (`SDL_AUDIODRIVER=dummy`), CNA `next` with `easy-gl` and `meta-gl`
checked out beside it.

## Builds

| Preset | `CNA_GRAPHICS_RENDERER` | Build | Notes |
| --- | --- | --- | --- |
| `opengles3` | `OPENGLES3` | ok | CNA's Linux default; the build used for all tests, screenshots and benchmarks. |
| `opengl33` | `OPENGL33` | ok | Desktop OpenGL 3.3 core through EasyGL; Mesa exposes GL 4.5 on llvmpipe. |
| `software` | `SOFTWARE` | ok | CNA's CPU rasteriser; no GL context is created (the window is still an SDL window). |
| `vulkan` | `VULKAN` | not built | No Vulkan ICD in the container (no `lavapipe` package); nothing to run it on. |

Every build is configured with `CNA_CNAEXT=OFF` and the same project options; only the
renderer variable differs. `ctest` runs against the `opengles3` build (196 tests in six registrations).

## Runs

Both scenes use `--no-save --no-audio --lockstep --spawn square --auto-drive 3
--traffic-warmup 20 --time 13:00 --time-scale 0 --weather cloudy --benchmark` so the simulated
state, the clock and the weather are identical on every renderer; the capture is the last frame
(frame 40 exterior, frame 36 cockpit). Times are CPU draw submission
per frame (llvmpipe rasterises on the CPU, so for the GL renderers this includes most of the
rasterisation; the SOFTWARE renderer rasterises inside the draw calls).

| Scene | Renderer | draw calls | triangles | draw submission | world | traffic | vehicle | mirror |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Town, chase camera | OPENGLES3 | 1253 | 1242k | 166 ms | 105 | 53 | 5 | - |
| Town, chase camera | OPENGL33 | 1253 | 1242k | 184 ms | 119 | 57 | 6 | - |
| Town, chase camera | SOFTWARE | 1253 | 1242k | 3599 ms | 2510 | 502 | 207 | - |
| Town, cockpit + mirror + cluster | OPENGLES3 | 1299 | 1245k | 223 ms | 117 | 61 | 5 | 39 |
| Town, cockpit + mirror + cluster | OPENGL33 | 1299 | 1245k | 235 ms | 118 | 69 | 6 | 40 |
| Town, cockpit + mirror + cluster | SOFTWARE | 1299 | 1245k | 4639 ms | 2378 | 536 | 705 | 644 |

Re-measured for Phase 13 with `scripts/renderer_compare.sh`, which prints the table above and
records what was actually done per renderer (see "What 'tested' means here" below).

Draw calls and triangle counts are identical across renderers for the same frame, which is the
expected result of renderer-independent culling. The SOFTWARE renderer is about twenty times
slower than llvmpipe on this scene (single-threaded rasterisation, per-pixel lighting of 1.24 M
triangles) and is only useful for conformance checks and for machines without any GL driver.

These numbers were re-measured on the Phase 12 map (6.4 x 7.6 km, five settlements); the Phase
11 set on the smaller map read 606 draw calls and 575k triangles for the same camera, at 121 ms
on OPENGLES3. OPENGL33 used to be a third faster than OPENGLES3 here and is now level with it:
the frame is dominated by the world and traffic passes, which do the same work on both.

## Image differences

Pixel comparison of the same frame (max channel difference per pixel, 1280 x 720), re-measured
for Phase 13 with `scripts/renderer_sheet.py`, which also builds the sheets below:

| Pair | mean difference | pixels differing by > 32 | > 96 |
| --- | --- | --- | --- |
| OPENGL33 vs OPENGLES3, town | 3.82 / 255 | 0.36 % | 0.000 % |
| SOFTWARE vs OPENGLES3, town | 5.49 / 255 | 1.70 % | 0.083 % |
| OPENGL33 vs OPENGLES3, cockpit | 0.45 / 255 | 0.00 % | 0.000 % |
| SOFTWARE vs OPENGLES3, cockpit | 2.32 / 255 | 1.04 % | 0.105 % |

Unchanged in character from Phase 12 to within a hundredth of a level, which is the point: the
visual work of Phase 13 -- the paint's sun glint, the convex wing mirrors, the headlamp beam, the
wet-road sheen, the softened stars, the tapered forest edges -- introduced **no** renderer-specific
divergence. Every one of them is ordinary XNA 4.0 surface (`EnvironmentMapEffect`, `BasicEffect`
fog, vertex colours, additive blending) and all three renderers agree about it.
| OPENGL33 vs OPENGLES3, instrument cluster target | 0.0 | 0 % | 0 % |
| SOFTWARE vs OPENGLES3, instrument cluster target | 0.07 / 255 | 0 % | 0 % |

![Town frame on the three renderers](screenshots/renderers/town.png)

![Cockpit frame on the three renderers](screenshots/renderers/cockpit.png)

What differs, from the comparison images (top left OPENGLES3, top right OPENGL33, bottom
left SOFTWARE, bottom right the SOFTWARE minus OPENGLES3 difference amplified four times):

- **Texture filtering at grazing angles.** On llvmpipe the `OPENGLES3` path shows streaks
  along the road where the asphalt wear tracks and aggregate alias in the distance; `OPENGL33`
  and `SOFTWARE` filter the same mip chain (generated on the CPU by `Image::UploadTexture`)
  smoothly. All three use `SamplerState::AnisotropicWrap`; the difference is the driver's
  anisotropic implementation, not project code. On a GPU with real anisotropic filtering the
  tracks are expected to stay sharp without streaks (see `docs/real-hardware-validation.md`).
- **Edges and thin geometry.** The difference images are dominated by one-pixel edge shifts
  (building silhouettes, window frames, the fence wire, tree card cut-outs): rasterisation
  rules and the alpha-test coverage differ by half a texel. No geometry is missing on any
  renderer.
- **Hedge and lit vertex colours.** The clipped hedge and the road vertex colours are a few
  levels brighter on `SOFTWARE` (its per-pixel lighting interpolates slightly differently);
  the sky gradient, fog, glass reflections, the vehicle ground shadow and the baked ground
  shadows match.
- **Render targets.** The instrument cluster (SpriteBatch into a 1024 x 448 target) is
  bit-identical between the GL renderers and within 0.07 levels on `SOFTWARE`; the mirror
  target (768 x 200, mirrored projection) matches on all three.
- **Text.** HUD and overlay text (premultiplied bitmap font) is identical.

Nothing in the runs required a renderer-specific branch or setting. The one known
renderer-dependent behaviour found during Phase 11, stencil buffer clears not being applied
after the second frame on the EasyGL path, was removed from the design rather than worked
around: the vehicle shadow is a stencil-free convex-hull blob and no code path relies on
stencil state.

## What "tested" means here (Phase 13)

"Three renderers were tested" has to mean something, so each renderer is recorded against the
same six words, and nothing is claimed that was not done. `scripts/renderer_compare.sh` performs
the first four automatically and prints this table; the last two are human steps and the script
says so rather than pretending otherwise.

| | configures | compiles | starts | renders | visually inspected | performance tested |
| --- | --- | --- | --- | --- | --- | --- |
| `OPENGLES3` | yes | yes | yes | yes | yes -- every screenshot in this repository | yes |
| `OPENGL33` | yes | yes | yes | yes | yes -- the two comparison sheets below | yes |
| `SOFTWARE` | yes | yes | yes | yes | yes -- the two comparison sheets below | yes (twenty times slower) |
| `VULKAN` | no | no | no | no | no | no |
| `DIRECTX*`, `METAL`, `WEBGL*` | no | no | no | no | no | no |

- **configures / compiles**: `cmake --preset <p>` succeeds and produces a binary.
- **starts**: the process runs the scene and exits cleanly.
- **renders**: a screenshot came out, with its own draw-call and triangle counts.
- **visually inspected**: a person compared the images. **Equal draw calls are not visual
  equivalence** -- the two GL paths submit identical geometry and still differ by 3.8/255 in the
  mean, mostly from anisotropic filtering and half-texel edge coverage, which is exactly why the
  image-difference table below exists and why this row is not inferred from the one above it.
- **performance tested**: the benchmark numbers in this page's tables.

Everything above is the container: Xvfb, Mesa llvmpipe, **no GPU**. On a real machine the whole
table should be re-run (`docs/real-hardware-validation.md`, section 7) and the differences
recorded rather than assumed to vanish; a renderer discrepancy that turns out to be in CNA is to
be isolated and documented there, never worked around with renderer-specific application code.

## Not covered

- `VULKAN`, `DIRECTX*`, `METAL`, `WEBGL*`: not available in the container. The project does
  not use any API outside the XNA 4.0 surface, so no source change is expected; the same three
  scenes should be captured and compared on a machine that has them
  (`docs/real-hardware-validation.md`, section 3).
- Multisampling: `PreferMultiSampling` is left at CNA's default; the captures above are not
  multisampled.

## Phase 14: Radeon 780M desktop conformance

The Phase 13 section above describes the old Xvfb container. This Phase 14 run used the
actual Debian 13 desktop (`DISPLAY=:0`), AMD Radeon 780M, Mesa 25.0.7, `radeonsi` for
OpenGL ES and RADV PHOENIX for Vulkan, 1280 × 720. Vulkan now configures, compiles, starts
and renders on this machine. `vulkaninfo --summary` and the CNA startup log both identify
the Radeon 780M; the run used `MESA_VK_DEVICE_SELECT=1002:15bf`. No project code selected a
renderer or accessed its internals.

The fixed town scene used `--no-save --no-audio --lockstep --spawn square --frames 40
--traffic-warmup 20 --time 13:00 --time-scale 0 --weather cloudy --view -78 9 -30 50 -6
--benchmark --screenshot` on both builds at the same source commit (`f61e7ed`). The
benchmark excludes 30 warmup frames and measures 10 frames. Both report 1215 project draw
submissions, 1,450,306 triangles, 203 visible terrain chunks, 84 road batches, 370 object
batches, 40 tree batches, 20 traffic cars (four drawn), and 36 pedestrians (four drawn).
Project draw submission averaged 9.46 ms on OPENGLES3 and 7.59 ms on Vulkan. These are
short conformance captures, not a renderer speed ranking: the desktop throttled this
unfocused window to about 1.1 seconds of wall time per frame.

The corresponding cockpit and 1024 × 448 live instrument-cluster targets were also captured
on both renderers from the same two-frame startup state. Manual side-by-side inspection found
the same world objects, glass, mirror, wheel, controls, gauge labels and lamps. The images
are close, not pixel-identical; edge coverage, filtering and antialiasing differ. Pixel
comparison uses maximum absolute RGB channel difference per pixel:

| OPENGLES3 vs Vulkan | Mean absolute RGB difference (R/G/B, of 255) | Pixels > 32 | Pixels > 96 |
| --- | --- | --- | --- |
| Town fixed camera | 0.716 / 0.683 / 0.680 | 0.65% | 0.196% |
| Cockpit startup | 0.899 / 0.873 / 0.819 | 0.687% | 0.252% |
| Instrument cluster target | 1.292 / 1.258 / 1.201 | 1.422% | 0.320% |

The current OPENGL33 build, running on the same Radeon through desktop OpenGL 4.6, reported
the same 1215 submissions and 1,450,306 triangles in the fixed town scene. Its town,
cockpit and cluster PNGs are byte-identical to OPENGLES3 (SHA-256 comparisons). Its 10-frame
project draw submission average was 11.73 ms under the same unfocused-window throttling;
that number cannot establish a performance ranking.

The SOFTWARE renderer also built and completed the same 40-frame town scene offscreen with
1215 submissions and 1,450,306 triangles. Its project draw submission averaged 3802.73 ms
per measured frame; this CPU rasterisation result is not a Radeon GPU timing. Compared with
OPENGLES3, mean absolute RGB difference was 3.978 / 4.031 / 3.975 levels of 255, with
0.968% of pixels differing by more than 32 and 0.049% by more than 96 in any channel.
The [paired town capture](screenshots/renderers/phase14-gles-software-town.jpg) (OPENGLES3
left, SOFTWARE right) shows the same geometry, surface colours and scene content. Differences
are mainly filtering and edge coverage. The startup cockpit and cluster targets were also
captured and visually inspected. The [cockpit pair](screenshots/renderers/phase14-gles-software-cockpit.jpg)
has the same mirror, road, controls and gauges; its mean RGB difference is 1.863 / 2.026 /
1.623 levels, with 1.925% of pixels over 32 and 0.068% over 96. The
[cluster pair](screenshots/renderers/phase14-gles-software-cluster.png) differs by just
0.006 / 0.005 / 0.006 mean levels, with no pixel over 32. Small differences are visible on
green terrain and edges, but no content is absent.

The three paired captures show OPENGLES3 on the left and Vulkan on the right:
[town](screenshots/renderers/phase14-gles-vulkan-town.jpg),
[cockpit](screenshots/renderers/phase14-gles-vulkan-cockpit.jpg), and
[cluster](screenshots/renderers/phase14-gles-vulkan-cluster.png).

There is no missing pass or renderer-specific application fix. The matched geometry and
inspected images matter more for conformance than matched draw counts alone. The historical
tables above remain labelled as Phase 13 results.

### Phase 14 tree atlas checkpoint (2026-09-25)

After adding two seeded silhouettes per tree species in one XNA texture atlas, all four
available renderers built and captured the same two-frame forest view at 1280 × 720:
`--no-save --no-audio --lockstep --spawn forest --frames 2 --time 13:00 --time-scale 0
--weather clear --view -228 5 -1280 0 -4`. OPENGLES3, OPENGL33 and Vulkan RADV used the
Radeon 780M desktop; SOFTWARE used CNA's offscreen CPU path. No project renderer branch or
renderer-native API was added. The four retained captures are
[OPENGLES3](screenshots/renderers/phase14-forest-gles3.png),
[OPENGL33](screenshots/renderers/phase14-forest-gl33.png),
[Vulkan](screenshots/renderers/phase14-forest-vulkan.png) and
[SOFTWARE](screenshots/renderers/phase14-forest-software.png).

The OPENGL33 frame is byte-identical to OPENGLES3. Visual inspection found the same crown
variants, trunk positions and alpha edges on Vulkan and SOFTWARE, without atlas seams or
missing vegetation. Against OPENGLES3, mean absolute RGB difference was
0.545 / 0.490 / 0.594 on Vulkan (0.694% of pixels differ by >32 in any channel) and
2.528 / 2.371 / 1.222 on SOFTWARE (0.442% >32). The Vulkan and SOFTWARE differences are
concentrated around foliage edges, filtering and ground texture; no renderer-specific
project fix was needed. These are conformance frames, not performance comparisons.

### Phase 14 cockpit trim checkpoint (2026-09-25)

After the centre radio gained a moulded surround and physical controls, the cockpit was
captured at 13:00, clear weather, square spawn, frame 3, 1280 × 720 on all four supported
renderers. All binaries include the subsequent byte-identical car geometry helper extraction.
The retained frames are [OPENGLES3](screenshots/renderers/phase14-console-gles3.jpg),
[OPENGL33](screenshots/renderers/phase14-console-gl33.jpg),
[Vulkan](screenshots/renderers/phase14-console-vulkan.jpg) and
[SOFTWARE](screenshots/renderers/phase14-console-software.jpg).

The controls, cluster, mirrors and road appear in all four. OPENGL33 differs from OPENGLES3
by at most one RGB level; the mean absolute RGB differences are 0.864 / 0.826 / 0.788 for
Vulkan and 1.966 / 2.110 / 1.703 for SOFTWARE. The differences are in shading and edge
coverage; no content is missing and no renderer-specific project code was added.

### Phase 14 local solid-line checkpoint (2026-09-25)

The newly authored solid centre-line section on `main` near E3 was captured at 13:00,
clear weather, frame 2, 1280 × 720, from `--view 1300 15 -330 40 -8` on OPENGLES3,
OPENGL33, Vulkan and SOFTWARE. The [before](screenshots/phase14/centre-section-before.jpg)
and [after](screenshots/phase14/centre-section-after.jpg) Radeon OPENGLES3 frames show
the dashed line changing to a continuous line on the same road geometry.

All four renderers show the same continuous stroke and road scene. OPENGL33 is byte-identical
to OPENGLES3. Mean absolute RGB differences against OPENGLES3 were 0.608 / 0.594 / 0.660
for Vulkan and 2.797 / 2.450 / 1.186 for SOFTWARE, mainly from foliage and ground
filtering. No renderer-specific application code was added.

### Phase 14 B 21 roadside sign checkpoint (2026-09-25)

After adding procedural B 21a/B 21b faces and plates at the E3 restriction, all four
renderers captured the same 1280 × 720 clear 13:00 view at frame 2 with
`--view 1160 8 -273 68 -7`. The retained
[Radeon OPENGLES3 view](screenshots/phase14/overtaking-sign.jpg) shows the B 21a plate
beside the authored restriction. OPENGL33 was byte-identical. Mean absolute RGB difference
against OPENGLES3 was 0.719 / 0.702 / 0.726 for Vulkan RADV and
7.766 / 7.745 / 4.515 for SOFTWARE. Visual inspection found the same plate and road
marking in all four; the larger software difference is mainly surface and vegetation tone.

### Phase 14 square planter checkpoint (2026-09-25)

The stone planters around the Lipová memorial were captured at the same fixed 1280 × 720,
13:00 clear, two-frame view (`--view -55 7 -65 180 -7`) with all four renderers. The
[before](screenshots/phase14/square-planters-before.jpg) and
[after](screenshots/phase14/square-planters-after.jpg) Radeon OPENGLES3 frames show the
two beds framing the memorial. Both are present in OPENGL33, Vulkan RADV and SOFTWARE.
OPENGL33 is byte-identical to OPENGLES3. Against OPENGLES3, mean absolute RGB difference
is 0.625 / 0.625 / 0.623 for Vulkan and 5.591 / 5.653 / 5.278 for SOFTWARE; 0.590% and
1.694% of pixels respectively differ by more than 32 in any channel. The differences are
mainly foliage and paved-surface tone. The same fixed view was also inspected in snow;
the shrubs remain dark green, matching the current forest foliage treatment but still
needing a future accumulation pass. No renderer-specific project code was added.

### Phase 14 static geometry extraction checkpoint (2026-09-25)

After the eight existing `WorldRenderer` terrain, road and paving builders moved verbatim
to `WorldRendererGeometry.cpp`, the fixed square scene above was rebuilt and captured
again on all four available renderers. The post-extraction PNG is byte-identical to its
own pre-extraction PNG on each path: OPENGLES3 and OPENGL33
`4a4191f323c93682e0dcf2080abf32893ec1e635640774ae5ec2558bb2dcf234`,
Vulkan RADV `6aec07ae1c6814cdd9d0cc168b42e35a4a9286f1cf51f0251edb1d0c28cdf8db`,
SOFTWARE `87007d0687853b28cbc952a274a9f39e2e5d95f1bb472732538b530a678a6ae6`.
This checks the rendered outcome as well as the unchanged method text.

### Phase 14 winter tree atlas checkpoint (2026-09-25)

After the tree cards gained snow accumulation on their upper crowns, the same fixed
forest view (`--spawn forest --frames 2 --time 13:00 --time-scale 0 --weather snow
--view -228 5 -1280 0 -4`) was captured at 1280 × 720. OPENGLES3 and OPENGL33
produced byte-identical PNGs. SOFTWARE showed the same snow-covered crowns and
terrain; its mean absolute RGB difference from OPENGLES3 was 1.634 / 1.526 /
1.847 levels of 255, with 0.572% of pixels differing by more than 32 in any
channel. The clear-weather OPENGLES3 forest frame remained byte-identical to
the pre-change capture.

Vulkan RADV still fails to draw this snow scene: CNA rejects the existing
40-byte terrain declaration (`Normal0@12 Vector3`) on its ordinary-indexed
route. The identical command also fails with the identical error in a separate
build of prior commit `a92d8f3`, before the winter atlas work. Vulkan's clear
forest frame succeeds. This is a pre-existing snow-pass conformance gap, not
evidence that the new atlas changes a vertex layout. The snow pass needs a
separate project-side correction using the public XNA API; it has not been
counted as conformant here.

### Phase 14 snow terrain vertex-layout correction (2026-09-25)

The terrain snow pass now draws a compact `PositionTexture` vertex view of each
existing terrain chunk with the same XNA `BasicEffect` and shared index buffer.
This removes the 40-byte dual-UV declaration from that single-texture pass
without selecting a renderer in application code. The same fixed snow view now
completes on Vulkan RADV. Its mean absolute RGB difference from OPENGLES3 is
0.391 / 0.382 / 0.378 levels of 255, with 0.453% of pixels differing by more
than 32 in any channel. OPENGL33 remains byte-identical to OPENGLES3; SOFTWARE
remains byte-identical to its pre-correction capture (and differs from OPENGLES3
by 1.634 / 1.526 / 1.847 mean levels). The corrected OPENGLES3 capture itself
is byte-identical to its pre-correction image. All four were captured at the
same scene, frame, resolution and weather. The extra terrain vertex allocation
is documented in `docs/performance.md`.

### Phase 14 pedestrian outerwear checkpoint (2026-09-25)

The fixed square view (`--spawn square --frames 2 --time 13:00 --time-scale 0
--weather clear --view -61 6.5 14 0 -4`) was captured at 1280 × 720 after
adding shared coat and cap meshes. All four renderers show the same nearby
walker in a rust-coloured coat and brimmed cap. OPENGL33 is byte-identical to
OPENGLES3; Vulkan RADV differs by 0.626 / 0.568 / 0.685 mean RGB levels and
SOFTWARE by 2.976 / 2.896 / 2.542, with 0.700% and 1.935% of pixels over 32
in any channel respectively. The difference images are dominated by tree and
ground filtering, with the same person visible on each path. The coat replaces
the shirt draw and the cap replaces the hair draw; no extra draw per walker
was introduced.

### Phase 14 cockpit binnacle checkpoint (2026-09-25)

After moving the Lipan's instrument face in front of the dashboard fascia and adding a
separate satin trim material, all four existing renderers built and completed the same
40-frame, 1280 × 720 cloudy cockpit scene on hidden Xvfb `:99`. The captures are
[OPENGLES3](screenshots/phase14/cockpit-review-opengles3-xvfb.png),
[OPENGL33](screenshots/phase14/cockpit-review-opengl33-xvfb.png),
[Vulkan](screenshots/phase14/cockpit-review-vulkan-xvfb.png), and
[SOFTWARE](screenshots/phase14/cockpit-review-software-xvfb.png). Each reported 1,230
instrumented main-view draws. Visual inspection found the full dial face, narrow rim and
separate fascia visible on every path. OPENGL33 differs from OPENGLES3 by mean absolute
RGB 0.353 / 0.360 / 0.334 (less than 0.001% of pixels exceed 32 in any channel);
Vulkan differs by 1.307 / 1.313 / 1.232 (0.766% over 32); SOFTWARE by
2.015 / 2.071 / 2.049 (1.467% over 32). The remaining differences are mostly surface
tone/filtering. This virtual-display conformance pass used Mesa llvmpipe for Vulkan;
the separate Radeon OPENGLES3 weather review is in `screenshots/phase14/README.md`.
No renderer-specific project code was added, and the public XNA-only checker passed.

### Phase 14 wing-mirror culling checkpoint (2026-09-25)

After culling mirror scenes whose glass is outside the cockpit camera frustum, all four
renderers rebuilt and completed the same 40-frame, 1280 × 720 cloudy cockpit scene on
virtual Xvfb `:99`: [OPENGLES3](screenshots/renderers/phase14-wing-cull-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-wing-cull-opengl33.png),
[Vulkan](screenshots/renderers/phase14-wing-cull-vulkan.png), and
[SOFTWARE](screenshots/renderers/phase14-wing-cull-software.png). Each reported 1230
main-view draws. The images were visually inspected: dashboard, cluster, rear mirror and
visible left wing reflection are present in every renderer. OPENGL33 is byte-identical to
OPENGLES3; Vulkan differs by mean absolute RGB 1.485 / 1.588 / 1.094 and 1.614% of pixels
over 32 in any channel; SOFTWARE by 2.203 / 2.365 / 1.940 and 2.310% over 32. These are
the established filtering and shading differences, with no missing mirror content. Vulkan
on this virtual display used the software ICD; the separate Radeon OPENGLES3 capture and
rightward mirror check are in `screenshots/phase14/`. `scripts/check_xna_only.py` passed
(230 application files scanned); no renderer-specific application path was added.

### Phase 14 narrow interior windshield trim checkpoint (2026-09-25)

The wide interior A-pillar quads were replaced by a narrow lip following each windscreen
rail. All four existing public-XNA renderer builds completed the same 40-frame cloudy
cockpit scene on dedicated virtual Xvfb `:103`:
[OPENGLES3](screenshots/renderers/phase14-narrow-pillar-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-narrow-pillar-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-narrow-pillar-software.png), and
[Vulkan](screenshots/renderers/phase14-narrow-pillar-vulkan.png). Each reported 1,230
main-view draws. Visual inspection found the same open left sight line, intact headliner,
cluster and mirrors in each image. Relative to OPENGLES3, mean absolute RGB differences
were 0.382 / 0.388 / 0.358 for OPENGL33, 1.332 / 1.338 / 1.257 for Vulkan, and
2.183 / 2.242 / 2.214 for SOFTWARE; the respective shares of pixels exceeding 32 in
any channel were below 0.001%, 0.784% and 1.665%. These captures are for conformance,
not Radeon timing. The separate hidden Radeon 800 × 480 seven-condition review is in
[`screenshots/phase14/`](screenshots/phase14/README.md). The public XNA checker passed
(233 application files scanned).

### Phase 14 snowy road-verge checkpoint (2026-09-25)

The road-distance tint and surface-specific snow opacity use only public XNA effects.
All four existing renderer targets rebuilt and rendered the same two-frame, 800 × 480
forest-road snow view on dedicated virtual Xvfb `:103`:
[OPENGLES3](screenshots/renderers/phase14-road-verge-snow-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-road-verge-snow-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-road-verge-snow-software.png), and
[Vulkan](screenshots/renderers/phase14-road-verge-snow-vulkan.png). Visual inspection found
the snow-covered verge continuous with the field in all four. OPENGL33 is byte-identical
to OPENGLES3. Relative to OPENGLES3, mean absolute RGB difference is 2.627/255 for
SOFTWARE and 1.373/255 for Vulkan; 4,922 and 4,653 pixels respectively exceed 32 in
any channel, mostly from established ground and tree filtering differences. The public
XNA-only check passed (233 files scanned). These virtual-display images are conformance
checks; the matched performance capture used hidden Radeon offscreen rendering.

### Phase 14 arched cockpit hood checkpoint (2026-09-25)

The five-section binnacle hood built and rendered on OPENGLES3, OPENGL33, SOFTWARE
and Vulkan at 1280 × 720 on dedicated virtual Xvfb `:103`:
[OPENGLES3](screenshots/renderers/phase14-arched-hood-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-arched-hood-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-arched-hood-software.png), and
[Vulkan](screenshots/renderers/phase14-arched-hood-vulkan.png). The arched shape and
uncovered dials are visible in all four. OPENGL33 is byte-identical to OPENGLES3;
SOFTWARE differs by 2.311/255 mean absolute RGB with 26,202 pixels above 32 in
any channel, Vulkan by 0.831/255 with 6,086 pixels above 32. The differences are
mostly the established surface filtering and shading variation. This conformance pass
opened no physical display. `scripts/check_xna_only.py` passed (233 files scanned).

### Phase 14 passenger dashboard pad checkpoint (2026-09-25)

After adding the passenger upper pad and recessed lid, all four public-XNA renderers
built and completed the same 40-frame 800 × 480 clear cockpit scene on dedicated virtual
Xvfb `:107`: [OPENGLES3](screenshots/renderers/phase14-passenger-pad-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-passenger-pad-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-passenger-pad-software.png), and
[Vulkan](screenshots/renderers/phase14-passenger-pad-vulkan.png). Each reported 1,208
draw submissions and 1,448,514 triangles for the complete frame. All show the same
panel, intact cluster, mirrors and road view. Relative to OPENGLES3, mean absolute RGB
differences were 0.291 / 0.309 / 0.270 for OPENGL33, 2.787 / 2.823 / 2.829 for
SOFTWARE and 2.146 / 2.136 / 2.038 for Vulkan; respectively 0, 10,865 and 6,499
pixels exceeded 32 in any channel. These virtual-display renders check geometry and
appearance, not Radeon performance. The separate hidden Radeon seven-condition review
is in [`screenshots/phase14/`](screenshots/phase14/README.md). No renderer-specific
application path was added.

### Phase 14 Czech shop fascia checkpoint (2026-09-26)

All four public-XNA renderer builds completed the same 40-frame, 800 × 480 clear
square view on dedicated virtual Xvfb `:109` after adding fascia boards and letters:
[OPENGLES3](screenshots/renderers/phase14-shop-sign-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-shop-sign-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-shop-sign-software.png), and
[Vulkan](screenshots/renderers/phase14-shop-sign-vulkan.png). Each reported 1,198 draw
submissions and 1,356,390 triangles for the complete frame. The `ELEKTRO` and
`HODINY` boards appear in all four images; adjacent shop windows and doors remain
visible. Relative to OPENGLES3, mean absolute RGB differences were 1.927 / 1.920 /
1.819 for OPENGL33, 5.574 / 5.645 / 5.514 for SOFTWARE and 3.388 / 3.340 / 3.251
for Vulkan; respectively 2,689, 10,648 and 8,512 pixels exceeded 32 in any
channel. The larger scene differences are visible in ground shading and texture
filtering, with no missing sign content. These captures are conformance evidence,
not GPU timings. No renderer-specific application code was added.

### Phase 14 sparse forest undergrowth checkpoint (2026-09-26)

The final low-shrub forest interior built and rendered on all four public-XNA backends
in the same 40-frame clear virtual Xvfb `:110` scene:
[OPENGLES3](screenshots/renderers/phase14-undergrowth-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-undergrowth-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-undergrowth-software.png), and
[Vulkan](screenshots/renderers/phase14-undergrowth-vulkan.png). Every backend reports
435 draws and 543,452 triangles and shows the same low shrub between trunks. Relative
to OPENGLES3, mean absolute RGB differences are 0.990 / 0.787 / 0.310 for OPENGL33,
1.323 / 1.094 / 0.507 for SOFTWARE and 2.450 / 2.227 / 1.857 for Vulkan;
respectively 33, 212 and 5,691 pixels exceed 32 in any channel. Vulkan's larger
difference is concentrated on ground shading, with no missing vegetation. The separate
matched clear/snow pairs and hidden Radeon checks are in
[`screenshots/phase14/`](screenshots/phase14/README.md). The public XNA boundary
remains intact; no renderer-specific project code was added.

### Phase 14 soft cockpit cowl checkpoint (2026-09-26)

The moulded defroster outlets and separate soft dashboard material built and rendered
in the same 40-frame, 800 × 480 clear cockpit scene on dedicated virtual Xvfb `:112`:
[OPENGLES3](screenshots/renderers/phase14-soft-cowl-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-soft-cowl-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-soft-cowl-software.png), and
[Vulkan](screenshots/renderers/phase14-soft-cowl-vulkan.png). Every backend reports
1,209 draw submissions and 1,451,464 triangles. The passenger pad, cowl, cluster,
mirror and road remain visible in each image. Relative to OPENGLES3, mean absolute RGB
differences are 0.290 / 0.307 / 0.269 for OPENGL33, 2.808 / 2.844 / 2.854 for SOFTWARE,
and 2.143 / 2.133 / 2.034 for Vulkan; respectively 0, 10,971 and 6,502 pixels exceed
32 in any channel. The differences follow the established filtering and shading
variation. Separate seven-condition hidden Radeon captures are in
[`screenshots/phase14/`](screenshots/phase14/README.md). Ten procedural-car tests and
the public-XNA boundary check pass; no renderer-specific project path was added.

### Phase 14 charcoal windscreen rail checkpoint (2026-09-26)

The inner windscreen rail changed material without moving geometry. The same 40-frame,
800 × 480 clear cockpit scene completed on dedicated virtual Xvfb `:113` for
[OPENGLES3](screenshots/renderers/phase14-charcoal-rail-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-charcoal-rail-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-charcoal-rail-software.png), and
[Vulkan](screenshots/renderers/phase14-charcoal-rail-vulkan.png). All four report 1,209
draw submissions and 1,451,464 triangles, with the charcoal pillar, intact cluster,
mirror and road visible. Relative to OPENGLES3, mean absolute RGB differences are
0.290 / 0.307 / 0.269 for OPENGL33, 2.802 / 2.840 / 2.845 for SOFTWARE, and
2.135 / 2.126 / 2.027 for Vulkan; respectively 0, 11,012 and 6,515 pixels exceed
32 in any channel. Differences follow the established filtering and shading variation.
The seven-condition hidden Radeon review is in
[`screenshots/phase14/`](screenshots/phase14/README.md). Ten procedural-car tests and
the public-XNA boundary check pass; no renderer-specific code was added.

### Phase 14 variable rural verge checkpoint (2026-09-26)

All four public-XNA renderers built and completed the same 40-frame, 800 × 480 snow
forest-road scene on dedicated virtual Xvfb `:114`:
[OPENGLES3](screenshots/renderers/phase14-verge-edge-opengles3.png),
[OPENGL33](screenshots/renderers/phase14-verge-edge-opengl33.png),
[SOFTWARE](screenshots/renderers/phase14-verge-edge-software.png), and
[Vulkan](screenshots/renderers/phase14-verge-edge-vulkan.png). Each reports 622 draw
submissions and 440,919 triangles, with a continuous snow-covered verge and road.
Relative to OPENGLES3, mean absolute RGB differences are 2.571 / 2.390 / 3.159 for
OPENGL33, 4.331 / 4.099 / 5.134 for SOFTWARE and 3.879 / 3.691 / 4.447 for Vulkan;
respectively 609, 5,699 and 5,430 pixels exceed 32 in any channel.

The virtual GLES3 llvmpipe image has a coarse tiled ground pattern absent from the
other three virtual paths and from the hidden Radeon GLES3 capture. A controlled
[two-frame build using the previous road-mesh source](screenshots/renderers/phase14-verge-edge-gles-old-source.png)
on the same Xvfb display reproduces the pattern. The far-right terrain pixels are
identical between that old-source frame and the new-source two-frame capture; all
pixels changing above 12/255 lie in the road/verge region. This discrepancy predates
the variable verge code. It remains documented under P14-052 rather than being
misattributed to this visual change. No renderer-specific application code was added.

### Phase 14 limewashed cottage checkpoint (2026-09-26)

The same 800 × 480 clear cottage-front scene completed on isolated Xvfb `:115` with
[OPENGL33](screenshots/phase14/cottage-stucco-opengl33.png),
[OPENGLES3](screenshots/phase14/cottage-stucco-opengles3-xvfb.png),
[SOFTWARE](screenshots/phase14/cottage-stucco-software.png), and
[Vulkan](screenshots/phase14/cottage-stucco-vulkan.png). All four show the new window
surrounds, corner strips and eaves frieze without covering the original openings.
The GLES3 llvmpipe road/ground pattern noted above is still present; the hidden Radeon
[after frame](screenshots/phase14/cottage-stucco-after.png) has no such pattern.
`scripts/check_xna_only.py` passes (233 files, 544 known XNA 4.0 types). No backend
specific code was added and no physical display was used.

### Phase 14 fog trunk checkpoint (2026-09-26)

After culling distant trunks with their foliage in dense fog, all four public-XNA
renderers built and completed the same 40-frame 800 × 480 forest-edge view on isolated
Xvfb `:116`: [OPENGL33](screenshots/renderers/phase14-fog-trunks-opengl33.png),
[OPENGLES3](screenshots/renderers/phase14-fog-trunks-opengles3.png),
[SOFTWARE](screenshots/renderers/phase14-fog-trunks-software.png), and
[Vulkan](screenshots/renderers/phase14-fog-trunks-vulkan.png). Each reported 316
main-view draw submissions, 297,397 triangles, 11 object and five foliage batches.
The stray bare trunks are absent in all four images. Virtual GLES3 still has the
previously isolated tiled ground pattern; Vulkan foliage filtering differs from GL
and software in fog, while the tree line and culling are consistent. The hidden Radeon
[matched pair](screenshots/phase14/README.md) verifies the correction on hardware.
The public-XNA checker passed (233 files, 544 known types); no backend-specific
application branch or physical desktop window was used.

### Phase 14 cockpit radio readout checkpoint (2026-09-26)

After adding the radio's separate self-lit material, all four public-XNA paths
completed the same 40-frame, 800 × 480 night cockpit scene on isolated Xvfb `:117`:
[OPENGL33](screenshots/renderers/phase14-cockpit-radio-opengl33.png),
[OPENGLES3](screenshots/renderers/phase14-cockpit-radio-opengles3.png),
[SOFTWARE](screenshots/renderers/phase14-cockpit-radio-software.png), and
[Vulkan](screenshots/renderers/phase14-cockpit-radio-vulkan.png). Each reports 1,011
scene draw submissions and 1,342.46k triangles. OPENGL33 and OPENGLES3 differ by
at most four RGB levels per channel; relative to OPENGL33, SOFTWARE and Vulkan
change 7,442 and 7,859 pixels above 12/255 respectively, mainly established
gauge-edge and shading differences. All four show the same readout and intact road,
gauges and controls. Seven-condition hidden Radeon hardware captures and matched
1280 × 720 radio pairs are in
[the cockpit review](screenshots/phase14/README.md). Ten procedural-car tests and
the public-XNA checker passed (233 files, 544 known types); no backend-specific
application code or physical desktop window was used. P14-052 remains open for
the final Phase 14 renderer pass.

### Phase 14 grouped forest undergrowth checkpoint (2026-09-26)

The fixed 40-frame 800 × 480 snow forest view on isolated Xvfb `:119` completed on
[OPENGL33](screenshots/renderers/phase14-forest-clump-opengl33.png),
[OPENGLES3](screenshots/renderers/phase14-forest-clump-opengles3.png),
[SOFTWARE](screenshots/renderers/phase14-forest-clump-software.png), and
[Vulkan](screenshots/renderers/phase14-forest-clump-vulkan.png). Each reports 695
scene draws and 883,067 triangles, and each shows both low shrubs with winter cover.
Relative to OPENGL33, SOFTWARE and Vulkan differ by 6,570 and 7,303 pixels above
12/255; the virtual GLES3 image differs by 62,654 pixels because the previously
isolated llvmpipe terrain tiling recurs. The paired hidden Radeon GLES3
[clear and snow captures](screenshots/phase14/README.md) have smooth terrain and
show the intended new neighbour. Two map placement tests and the public-XNA check
pass (233 files, 544 known types); no backend-specific application path or physical
desktop window was used. P14-052 remains open for the final renderer pass.
