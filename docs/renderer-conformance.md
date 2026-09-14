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
renderer variable differs. `ctest` runs against the `opengles3` build (149 tests).

## Runs

Both scenes use `--no-save --no-audio --lockstep --spawn square --auto-drive 3
--traffic-warmup 20 --benchmark` so the simulated state is identical on every renderer; the
capture is the last frame (frame 40 exterior, frame 36 cockpit). Times are CPU draw submission
per frame (llvmpipe rasterises on the CPU, so for the GL renderers this includes most of the
rasterisation; the SOFTWARE renderer rasterises inside the draw calls).

| Scene | Renderer | draw calls | triangles | draw submission | world | traffic | vehicle | mirror |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Town, chase camera | OPENGLES3 | 606 | 575k | 121 ms | 76 | 31 | 11 | - |
| Town, chase camera | OPENGL33 | 606 | 575k | 97 ms | 60 | 24 | 9 | - |
| Town, chase camera | SOFTWARE | 606 | 575k | 1805 ms | 1264 | 95 | 158 | - |
| Town, cockpit + mirror + cluster | OPENGLES3 | 869 | 832k | 148 ms | 64 | 25 | 4 | 51 |
| Town, cockpit + mirror + cluster | OPENGL33 | 869 | 832k | 157 ms | 72 | 28 | 4 | 50 |
| Town, cockpit + mirror + cluster | SOFTWARE | 869 | 832k | 2906 ms | 1326 | 182 | 530 | 580 |

Draw calls and triangle counts are identical across renderers for the same frame, which is the
expected result of renderer-independent culling. The SOFTWARE renderer is about fifteen times
slower than llvmpipe on this scene (single-threaded rasterisation, per-pixel lighting of 575k
triangles) and is only useful for conformance checks and for machines without any GL driver.

## Image differences

Pixel comparison of the same frame (max channel difference per pixel, 1280 x 720):

| Pair | mean difference | pixels differing by > 32 | > 96 |
| --- | --- | --- | --- |
| OPENGL33 vs OPENGLES3, town | 4.1 / 255 | 0.7 % | 0 % |
| SOFTWARE vs OPENGLES3, town | 6.5 / 255 | 3.4 % | 0.09 % |
| OPENGL33 vs OPENGLES3, cockpit | 0.4 / 255 | 0 % | 0 % |
| SOFTWARE vs OPENGLES3, cockpit | 2.3 / 255 | 1.2 % | 0.13 % |
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

## Not covered

- `VULKAN`, `DIRECTX*`, `METAL`, `WEBGL*`: not available in the container. The project does
  not use any API outside the XNA 4.0 surface, so no source change is expected; the same three
  scenes should be captured and compared on a machine that has them
  (`docs/real-hardware-validation.md`, section 3).
- Multisampling: `PreferMultiSampling` is left at CNA's default; the captures above are not
  multisampled.
