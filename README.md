# cna-car-simulator

A realistic passenger-car driving simulator set in a fictional Czech landscape, written in
C++23 on the **XNA 4.0-compatible public API** of the [CNA](https://github.com/libcna/cna)
framework (branch `next`) and [Sharp Runtime](https://github.com/libcna/sharp-runtime)
(branch `next`).

There are no jobs, missions, deliveries or economy. You start a car, drive through a Czech
town, its outskirts, the countryside and a forest, meet traffic, and enjoy driving.

## Status

Early development (milestone M0: project skeleton). See [`plan.md`](plan.md) for the
authoritative plan, task ledger and milestone status. Nothing in this README claims a feature
that `plan.md` does not mark as done.

## Requirements

- CMake 3.23+, Ninja (recommended), a C++23 compiler (GCC 13+, Clang 18+, MSVC 19.38+).
- Sibling checkouts (the build consumes them with `add_subdirectory()`):
  - `../cna` -- CNA, branch **`next`**, with submodules `third_party/SDL`,
    `third_party/SDL_image`, `third_party/SDL_mixer` and `vendor/googletest`.
  - `../sharp-runtime` -- Sharp Runtime, branch **`next`**.
  - `../easy-gl` and `../meta-gl` when building the OpenGL family renderers (`OPENGLES3`, the
    Linux default, `OPENGL33`, ...).
- Linux packages for SDL3 (X11/Wayland development headers, ALSA/PulseAudio), OpenGL ES/EGL
  development headers for the GL renderers; FreeType is optional (only for CNA's font pipeline,
  which this project does not use).

## Building

```bash
git clone --branch next https://github.com/libcna/cna.git
git -C cna submodule update --init third_party/SDL third_party/SDL_image third_party/SDL_mixer vendor/googletest
git clone --branch next https://github.com/libcna/sharp-runtime.git
git clone https://github.com/libcna/easy-gl.git
git clone https://github.com/libcna/meta-gl.git
git clone https://github.com/libcna/cna-car-simulator.git
cd cna-car-simulator
cmake --preset opengles3          # or: default, opengl33, vulkan, software, debug
cmake --build --preset opengles3 -j
ctest --preset opengles3
./build/opengles3/bin/cna-car-simulator
```

Pass `-DCARSIM_CNA_ROOT=/path/to/cna -DCARSIM_SHARP_RUNTIME_ROOT=/path/to/sharp-runtime`
when the checkouts are elsewhere. The renderer is CNA's `CNA_GRAPHICS_RENDERER` option; the
simulator never depends on which renderer was selected.

Headless smoke run (no window interaction, useful under Xvfb):

```bash
./build/opengles3/bin/cna-car-simulator --frames 60 --screenshot shot.png --width 1280 --height 720
```

## Controls (planned defaults)

| Action | Key |
| --- | --- |
| Accelerator / brake | `W` / `S` |
| Steer | `A` / `D` |
| Clutch (manual mode) | `Q` |
| Gear up / down | `Left Shift` / `Left Ctrl` |
| Neutral / reverse (manual) | `N` / `R` |
| Automatic selector P/R/N/D | `P` / `R` / `N` / `G` |
| Toggle automatic / manual | `T` |
| Start / stop engine | `E` |
| Handbrake | `Space` |
| Indicators left / right, hazard | `,` / `.` / `H` |
| Headlights | `L` |
| Horn | `B` |
| Camera (cockpit / exterior) | `C` |
| Help | `F1` |
| Debug overlay | `F3` |
| Quit | `Esc` |

The final layout is documented in the in-game help and kept in sync with this table.

## Design summary

- **API boundary**: only the XNA 4.0 surface of CNA plus Sharp Runtime is used. CNAEXT,
  `EXT` extensions, renderer internals and platform libraries are prohibited and checked by
  `scripts/check_xna_only.py` (a CTest). See [`docs/api-boundary.md`](docs/api-boundary.md) and
  [`docs/framework-findings.md`](docs/framework-findings.md).
- **Map system**: JSON source maps with a versioned schema, an explicit road/lane graph, and
  geometry generated from it. See [`docs/map-format.md`](docs/map-format.md).
- **Vehicles**: data-driven definitions (mass, geometry, engine curve, gearboxes, fuel,
  suspension, brakes, dashboard mapping); a project-owned rigid-body/tyre simulation.
- **Assets**: procedural by default; every external asset is recorded with provenance and
  licence in [`assets/ASSETS.md`](assets/ASSETS.md) and `assets/manifest.json`.

## Development and testing

```bash
ctest --preset opengles3 -L unit          # unit tests (no display needed)
ctest --preset opengles3 -L static        # XNA-only API check
xvfb-run -a ctest --preset opengles3 -L display   # smoke tests that open a window
python3 scripts/check_xna_only.py --root .
```

## Licensing

Source code, documentation and project-authored data: MIT (see [`LICENSE`](LICENSE)). CNA is
licensed under the Microsoft Public License (Ms-PL); Sharp Runtime under its own licence; both
are separate dependencies. Third-party assets: see `assets/ASSETS.md`.
