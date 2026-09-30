# Renderer conformance

The simulator uses only CNA's XNA 4.0-compatible public API (`docs/api-boundary.md`, enforced
by `scripts/check_xna_only.py`), so the same source builds against every CNA renderer. The
renderer is CNA's `CNA_GRAPHICS_RENDERER` cache variable, set by the CMake presets
(`opengles3`, `opengl33`, `vulkan`, `software`, `default`); `CNA_CNAEXT` is always `OFF`. There
is no renderer-specific code in the project, and there must not be: a difference that turns out
to be in CNA is isolated, recorded in `docs/framework-findings.md` and reported upstream, never
worked around with a renderer branch in application code.

Git history holds the full image-by-image conformance log (per-change captures on all four
renderers, pixel statistics and hashes) up to September 2026.

## Status (September 2026)

"Tested" is recorded with six words, and nothing is claimed that was not done: *configures*,
*compiles*, *starts*, *renders*, *visually inspected*, *performance tested*.

| Renderer (preset) | Status | Where it was run |
| --- | --- | --- |
| `OPENGLES3` (`opengles3`) | all six; the reference for every test, screenshot and benchmark | AMD Radeon 780M (`radeonsi`), and Xvfb with Mesa llvmpipe |
| `OPENGL33` (`opengl33`) | all six | same two environments; on the Radeon its images are byte-identical to OPENGLES3 in almost every fixed scene |
| `VULKAN` (`vulkan`) | all six | Radeon 780M with RADV on a real display; Mesa's software Vulkan driver on Xvfb |
| `SOFTWARE` (`software`) | all six; about 20 times slower than llvmpipe (3.6–3.8 s a frame at 1280 × 720) | CNA's CPU rasteriser, offscreen and on Xvfb |
| `WEBGL2` (Emscripten) | configures, compiles, starts, renders in a browser; no side-by-side comparison recorded | the browser build packages `content/` and keeps a deeper audio queue (`simulator/CMakeLists.txt`, `docs/audio-design.md`) |
| Windows / MSVC | compiles; the tests carry MSVC Debug time limits and a Windows smoke registration (`tests/CMakeLists.txt`); no renderer comparison recorded | -- |
| `DIRECTX*`, `METAL`, others | not tried | -- |

The last common check ran after the final visual changes of September 2026: the same
800 × 480 clear-square scene, 40 lockstep frames, mirrors off, on all four desktop renderers.
Each reported exactly 620 main-view submissions / 787,905 triangles and 690 indexed 3D
submissions; the images were inspected, and OPENGL33 and OPENGLES3 were byte-identical.
Repeat this check after any change to rendering or draw submission.

## Known differences

None of these needed project code, and none is missing content:

- **Magnitude.** Mean absolute RGB difference against OPENGLES3, same frame: OPENGL33 0–1 of
  255 (byte-identical on the Radeon), Vulkan about 0.4–2.5 (3.9 in one snowy verge scene),
  SOFTWARE about 1.3–5.6 (7.8 in one roadside sign scene). The pixels that differ are mostly
  one-pixel edge shifts on silhouettes, window frames, fence wire and alpha-tested tree cards
  (rasterisation rules and alpha-test coverage differ by half a texel), plus ground and
  foliage filtering.
- **SOFTWARE lighting** interpolates slightly differently: hedges and road vertex colours come
  out a few levels brighter; sky, fog, glass, the car's ground shadow and baked shadows match.
- **Anisotropic filtering on llvmpipe.** OPENGLES3 on llvmpipe shows streaks along the asphalt
  at grazing angles; OPENGL33, SOFTWARE and the Radeon do not. It is the driver's filtering,
  not project code (all use `SamplerState::AnisotropicWrap` over a CPU-built mip chain).
- **Tiled ground on Xvfb GLES3.** Since late September 2026 the OPENGLES3 path on Xvfb/llvmpipe
  shows a coarse tiled pattern on terrain and paving that no other virtual renderer and not the
  Radeon GLES3 capture shows. A build of the older road-mesh source reproduces it, so it is not
  caused by the verge change it was first noticed with. It has not been isolated further (see
  `ROADMAP.md`).
- **Render targets.** The 1024 × 448 instrument cluster is bit-identical between the two GL
  renderers, within 0.07 levels on SOFTWARE and about 1.3 levels on Vulkan. The mirror targets
  match. HUD text is identical everywhere.
- **Vulkan and the 40-byte dual-UV layout.** In September 2026 CNA's Vulkan renderer rejected
  the terrain's 40-byte declaration (`Normal0@12 Vector3`) when the single-texture snow pass
  drew it. The snow pass now draws a compact `VertexPositionTexture` copy of each chunk with the
  shared index buffer; that costs 52.9 MiB (`docs/performance.md`, Memory) and left the
  OPENGLES3 and SOFTWARE images byte-identical.
- **Multisampling.** Native builds set `PreferMultiSampling` (CNA then requests 8x); the web
  build does not. The README pictures, taken on Xvfb with llvmpipe, are not multisampled.

## How to compare renderers

Build the presets you want (`cmake --preset <p> && cmake --build --preset <p>`), then:

```bash
scripts/renderer_compare.sh opengles3 opengl33 software vulkan            # town, chase camera
scripts/renderer_compare.sh --scene cockpit opengles3 opengl33 software
python3 scripts/renderer_sheet.py build/renderers town build/renderers/town-sheet.png
```

`renderer_compare.sh` runs the same deterministic frame (square spawn, 13:00 frozen, cloudy,
traffic warmed up, one simulation step per frame) through each built preset via
`scripts/run_headless.sh`, and prints per preset whether it was built, started and rendered,
with its draw calls and draw time. Images, JSON and logs go to `build/renderers/`; a preset
that is not built is reported as such, not skipped. `renderer_sheet.py` (needs Pillow) builds a
2 × 2 sheet of OPENGLES3, OPENGL33, SOFTWARE and the amplified SOFTWARE difference, and prints
each pair's mean difference and the share of pixels off by more than 32 and 96 levels.

For any other view, run each binary with the same fixed state and compare the PNGs, e.g.:

```bash
for r in opengles3 opengl33 vulkan software; do
  scripts/run_headless.sh build/$r/bin/cna-car-simulator --no-save --no-audio --lockstep \
      --spawn forest --frames 2 --time 13:00 --time-scale 0 --weather snow \
      --view -228 5 -1280 0 -4 --benchmark --benchmark-json build/renderers/$r-forest.json \
      --screenshot build/renderers/$r-forest.png
done
```

Draw calls and triangles must match exactly between renderers -- the scene is deterministic, so
a difference is a culling bug. Timings will differ and are not a renderer ranking unless they
come from the same GPU. **Equal draw counts are not visual equivalence**: open the images side
by side and record what differs. Never run captures on someone's live desktop: use Xvfb (what
`run_headless.sh` starts when no `DISPLAY` is set) or the hidden SDL offscreen path described
in `docs/performance.md`. Xvfb has no GPU, so Vulkan there runs on Mesa's software driver.
