# Renderer conformance

The simulator talks only to the XNA 4.0-compatible public API of CNA (`docs/api-boundary.md`,
enforced by `scripts/check_xna_only.py`), so the same binary source builds against every CNA
renderer. This page records the renderers that were built and run in the development
container and what differed between them. There is no renderer-specific code in the project;
the renderer is chosen with CNA's `CNA_GRAPHICS_RENDERER` cache variable through the CMake
presets (`opengles3`, `opengl33`, `software`, `vulkan`, `default`).

Environment: Linux container, Xvfb 1280 x 720, Mesa llvmpipe (software OpenGL, 4 threads),
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
renderer variable differs. `ctest` runs against the `opengles3` build (178 tests).

## Runs

Both scenes use `--no-save --no-audio --lockstep --spawn square --auto-drive 3
--traffic-warmup 20 --time 13:00 --time-scale 0 --weather cloudy --benchmark` so the simulated
state, the clock and the weather are identical on every renderer; the capture is the last frame
(frame 40 exterior, frame 36 cockpit). Times are CPU draw submission
per frame (llvmpipe rasterises on the CPU, so for the GL renderers this includes most of the
rasterisation; the SOFTWARE renderer rasterises inside the draw calls).

| Scene | Renderer | draw calls | triangles | draw submission | world | traffic | vehicle | mirror |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Town, chase camera | OPENGLES3 | 1253 | 1242k | 176 ms | 111 | 58 | 6 | - |
| Town, chase camera | OPENGL33 | 1253 | 1242k | 174 ms | 110 | 56 | 6 | - |
| Town, chase camera | SOFTWARE | 1253 | 1242k | 3571 ms | 2485 | 500 | 207 | - |
| Town, cockpit + mirror + cluster | OPENGLES3 | 1299 | 1245k | 240 ms | 115 | 61 | 4 | 58 |
| Town, cockpit + mirror + cluster | OPENGL33 | 1299 | 1245k | 237 ms | 114 | 61 | 5 | 56 |
| Town, cockpit + mirror + cluster | SOFTWARE | 1299 | 1245k | 4793 ms | 2353 | 532 | 708 | 821 |

Draw calls and triangle counts are identical across renderers for the same frame, which is the
expected result of renderer-independent culling. The SOFTWARE renderer is about twenty times
slower than llvmpipe on this scene (single-threaded rasterisation, per-pixel lighting of 1.24 M
triangles) and is only useful for conformance checks and for machines without any GL driver.

These numbers were re-measured on the Phase 12 map (6.4 x 7.6 km, five settlements); the Phase
11 set on the smaller map read 606 draw calls and 575k triangles for the same camera, at 121 ms
on OPENGLES3. OPENGL33 used to be a third faster than OPENGLES3 here and is now level with it:
the frame is dominated by the world and traffic passes, which do the same work on both.

## Image differences

Pixel comparison of the same frame (max channel difference per pixel, 1280 x 720):

| Pair | mean difference | pixels differing by > 32 | > 96 |
| --- | --- | --- | --- |
| OPENGL33 vs OPENGLES3, town | 3.8 / 255 | 0.36 % | 0 % |
| SOFTWARE vs OPENGLES3, town | 5.5 / 255 | 1.7 % | 0.08 % |
| OPENGL33 vs OPENGLES3, cockpit | 0.45 / 255 | 0 % | 0 % |
| SOFTWARE vs OPENGLES3, cockpit | 2.3 / 255 | 1.1 % | 0.11 % |
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
