# Real-hardware validation

The older curated screenshots and Phase 13 measurements were made with Xvfb and Mesa
**llvmpipe**. Phase 14 also has a first run on the actual Radeon 780M desktop; see
`docs/performance.md`. Keep the renderer and hardware with every measurement. A number that
was not measured on the machine it claims to describe is worse than no number at all.

The reference machine for this project's real-hardware runs is a **Debian 13 desktop with an
AMD Radeon 780M** (integrated RDNA 3), a real display, normal audio and a keyboard and mouse.
Nothing is written for that hardware specifically -- it is one representative mid-range Linux
machine, and the procedure below works on any PC with a GPU.

Everything here is a copy-and-paste command. The goal is that somebody can pull the branch and
have useful results inside half an hour.

---

## 0. What you need

- Debian 13 (or any Linux with a working GL driver), a C++23 compiler, CMake >= 3.24, Ninja.
- Checkouts of **CNA** (`next`) and **Sharp Runtime** (`next`). Either as siblings of this
  repository, or anywhere, passed with `-DCARSIM_CNA_ROOT=` and `-DCARSIM_SHARP_RUNTIME_ROOT=`.
- `python3` (3.10+) for the static checks and the benchmark report; Pillow only if you want the
  screenshot comparison sheets.
- On Debian 13, the driver side is normally already there for a Radeon 780M
  (`mesa-vulkan-drivers`, `libgl1-mesa-dri`); `glxinfo -B` should name `AMD Radeon Graphics
  (radeonsi, gfx1103...)` rather than `llvmpipe`. **If it says llvmpipe, stop and fix the
  driver** -- otherwise you will measure the same software rasteriser the container does.

## 1. Build

```bash
git clone <this repository> cna-car-simulator && cd cna-car-simulator
git checkout main

cmake --preset opengles3 -DCARSIM_CNA_ROOT=../cna -DCARSIM_SHARP_RUNTIME_ROOT=../sharp-runtime
cmake --build build/opengles3 -j"$(nproc)"

cmake --preset opengl33  -DCARSIM_CNA_ROOT=../cna -DCARSIM_SHARP_RUNTIME_ROOT=../sharp-runtime
cmake --build build/opengl33 -j"$(nproc)"          # the second renderer, for the comparison
```

Presets: `opengles3` (default), `opengl33`, `software` (Mesa llvmpipe -- useful as a control),
`vulkan` (needs an ICD). The build copies `content/` next to the executable; after editing map
or vehicle JSON either rebuild or pass `--content content`.

## 2. Tests before anything else

```bash
ctest --preset opengles3 --output-on-failure      # 6 registrations
python3 scripts/check_xna_only.py                 # XNA-only API boundary
python3 scripts/check_assets.py                   # asset provenance
python3 tools/maps/build_map.py --check           # the map matches its generator
./build/opengles3/bin/carsim-mapvalidate content/maps/lipova
```

All six ctest registrations must pass. `simulator_smoke` needs a display; on a desktop it uses
the real one, so do not run it over ssh without `DISPLAY`.

**Record**: compiler and version, `git rev-parse HEAD` of this repository, of CNA and of Sharp
Runtime, and the ctest summary line.

## 3. First run and the diagnostic overlay

```bash
./build/opengles3/bin/cna-car-simulator --debug-overlay
```

`F3` toggles the overlay at any time; it is off in a normal session. It reports, all from this
project's own instrumentation (no renderer internals are read):

| section | what to look at |
| --- | --- |
| `fps / frame` | frames per second, mean frame time and the **worst 1 %** over the last 3 s |
| `cpu update / draw` | our own two halves of the frame, in ms |
| `update split` | vehicle physics, collision, traffic AI, audio |
| `draw split` | sky, world, traffic, car, hud -- where the submission time goes |
| `cluster / mirror` | the two off-screen passes, the mirror's size and its update interval |
| `driving` | speed, rpm, gear, pedals, steering, per-wheel slip and load |
| `environment` | clock, weather preset, cloud/rain/wetness, sun elevation, lamp factor |
| `world` | drawn and culled batches, traffic alive/drawn per LOD, draw calls and triangles |

On a GPU the `draw` number is CPU submission only; 10–17 ms was measured on the reference
Radeon 780M in the first Phase 14 town suite. It does not isolate GPU execution. The `frame`
number includes presentation and can be badly distorted if the desktop throttles an unfocused
window; some first-run night scenes were limited to one present per second despite much shorter
project draw times. Confirm the window is active before treating frame wall time as FPS.

**Record** a photo or `F12` screenshot of the overlay in town, and the renderer line CNA prints
on stdout at start-up.

## 4. The manual checklist

Tick each; note anything that differs. (Keys are the defaults; `F1` shows the current bindings.)

- [ ] Window opens at 1280 x 720, `--fullscreen` works, `Esc` quits and writes the save file.
- [ ] `E` cranks and starts the engine: starter whine, catch, idle settling near 850 rpm.
- [ ] Automatic (`T` toggles, `F` selects drive): pulls away smoothly, reaches 50 km/h in town
      without wheel spin, upshifts audible as a short load dip, kickdown on full throttle.
- [ ] Manual: `Q` clutch, `Left Shift` / `Left Ctrl` gears, `1`-`6` direct. Pulling away in
      first without touching the throttle is possible; dropping the clutch at idle stalls.
- [ ] Reverse (`R`), parking manoeuvres at walking pace, handbrake (`Space`).
- [ ] `C` cockpit: cluster live and readable, wheel turns with `A`/`D`, mirror shows the road
      behind, `M` hides it.
- [ ] `,` `.` `H` indicators; `L` headlights, `K` high beam; `B` horn.
- [ ] Traffic keeps lane and distance, yields correctly, stops at red and goes at green at the
      signalised junction east of the square (`--spawn kostel` starts 90 m short of it).
- [ ] Cars overtake a slow bus/lorry only with enough sight and return room, and respect solid
      and direction-specific centre lines plus pedestrian crossings.
- [ ] `G` enters/exits walking from a parked car with the engine off; movement starts and stops
      smoothly, slopes and nearby traffic remain solid, and footsteps track distance walked.
- [ ] `J` enters/exits helicopter mode without altering normal-car controls afterward; `O`
      still cycles the optional turbo, ultra and ultra ultra modes in both modes.
- [ ] Weather cycles with `F4`; rain, snow and fog all render and sound coherently, rain changes
      braking, snow changes grip, and fog remains drivable with headlamps and traffic signals.
- [ ] Time: `F6` and `F7` move the clock an hour, `F8` freezes it; sunset and night look
      continuous, with no step in the sky or on the ground at any hour.
- [ ] `Backspace` puts the car back on the road; `F5` resets the trip meter; `F12` screenshots.

## 5. The benchmark suite

The scenarios are deterministic: a fixed route driven by the autopilot over the real physics,
a fixed traffic seed and warm-up, a frozen clock and a fixed weather preset, one simulation
step per drawn frame. Two runs on the same machine differ only by noise, which is what makes a
before/after comparison meaningful.

```bash
scripts/benchmark_suite.sh --label "Debian 13 / Radeon 780M / opengles3"
python3 scripts/benchmark_report.py build/benchmarks --label "Debian 13 / Radeon 780M / opengles3"
```

That runs eight scenes -- clear day, rain, clear night and rainy night, each from the exterior
and the cockpit camera -- at 1280 x 720 for 900 frames each, and prints the Markdown tables that
go into `docs/performance.md`. Useful variations:

```bash
scripts/benchmark_suite.sh --route forest              # forest instead of town
scripts/benchmark_suite.sh --route country
scripts/benchmark_suite.sh --scenes "night_cockpit rainynight_cockpit"    # the expensive pair
scripts/benchmark_suite.sh --width 1920 --height 1080
scripts/benchmark_suite.sh --bin build/opengl33/bin/cna-car-simulator --out build/bench-gl33
scripts/benchmark_suite.sh --quick                     # 320x200, 240 frames, for a smoke check
```

To compare before and after a change, keep the first run and pass it as the baseline:

```bash
cp -r build/benchmarks build/bench-before
# ... make the change, rebuild ...
scripts/benchmark_suite.sh
python3 scripts/benchmark_report.py build/benchmarks --against build/bench-before --label "after X"
```

**Record** the two tables the report prints, plus the JSON files themselves.

### Driving a route by hand

```bash
./build/opengles3/bin/cna-car-simulator --route town --route-stay --debug-overlay
./build/opengles3/bin/cna-car-simulator --route forest --cockpit --time 22:30 --lights
```

`--route` drives one of the routes defined in `content/maps/lipova/traffic.json` (`town`,
`country`, `forest`) with the autopilot and exits at the end; `--route-stay` keeps the game
running so you can watch it, and the keyboard still works for the camera and the overlays.

## 5b. The full-map validation drive

It must be hard for a broken road or junction to survive just because it is far from the main
spawn. Most of that is automated, and runs in `ctest`:

```bash
./build/opengles3/bin/carsim-mapvalidate content/maps/lipova
```

It walks the whole network and reports:

| check | what it would catch |
| --- | --- |
| every road produces a drivable piece; no degenerate junction patch | a road or junction that did not build |
| every lane reachable from lane 0, and from **every player spawn** | a settlement you can spawn in but not drive out of |
| dead-end lanes and lanes under 5 m | a road that stops in mid-air, a sliver piece |
| max grade over every road | an impossible climb |
| the ground under **every lane and every junction connector**, at one metre | a road or junction that does not sit on its terrain -- the lip a wheel falls off |
| every named route plans from its spawn | a benchmark or test route that quietly stopped being drivable |
| every spawn within 6 m of a lane | a spawn in a field |
| signs, props, buildings and trees inside the terrain, off the carriageway | content in the road |

What is *not* automated, and what a person should do on a real machine:

1. `--route town`, `--route country` and `--route forest` end to end, watching for texture seams,
   wrong markings, floating sections, hard elevation changes and odd junction geometry.
2. Drive from each of the nine spawns (`square`, `forest`, `fields`, `east`, `kostel`, `brezi`,
   `podhaji`, `mesto`, `kamenice`) for a couple of minutes in each direction.
3. The gravel forest track and its turning loop, at night, with headlights.
4. One lap of the southern ring (`east_ring`) and the north-eastern road to Nové Město, which are
   the newest roads and the least driven.

Report anything found the way section 10 describes; if a `--route` run reproduces it, it can be
turned into a regression test in `tests/Traffic/RouteDriverTests.cpp` the same day.

## 6. Screenshots to capture

Capture the curated set so the pictures line up beside the container ones in
`docs/screenshots/`:

```bash
scripts/capture_set.sh build/opengles3/bin/cna-car-simulator /tmp/hw-shots
```

Then compare `/tmp/hw-shots` with `docs/screenshots/` and note: sun direction and shadow
darkness, texture filtering (anisotropic filtering on a GPU sharpens the road wear tracks that
llvmpipe blurs), alpha-tested tree cards (edge halos), fog banding, z-fighting on road markings
or the car's ground shadow, and anything missing.

Also take one `F12` frame after a few minutes of real driving: real traffic state, real camera
motion, real weather transition.

## 7. Renderer comparison

Run the same scene through two renderers and compare both the numbers and the pictures:

```bash
for r in opengles3 opengl33; do
  ./build/$r/bin/cna-car-simulator --no-save --no-audio --lockstep --spawn square \
      --frames 120 --traffic-warmup 40 --time 13:00 --time-scale 0 --weather clear \
      --benchmark --benchmark-json /tmp/$r.json --screenshot /tmp/$r.png
done
```

Draw calls and triangles must match exactly -- the scene is deterministic, so a difference is a
culling bug. Frame times may differ a great deal and that is expected. Record what was actually
done for each renderer, using the vocabulary of `docs/renderer-conformance.md`: *configures*,
*compiles*, *starts*, *renders*, *visually inspected*, *performance tested*. Do not claim visual
equivalence from equal draw counts.

## 8. Sound

With a real device: engine at idle, under load and on the overrun; the gear-change dip; tyre
noise on asphalt versus the forest gravel; rain on the roof; wind above 80 km/h; horn; the
indicator relay; an impact against a fence. Note clicks at block boundaries (a defect) and the
level balance. `Page Up` / `Page Down` set the master volume.

## 9. What to send back

- **Machine**: CPU, GPU, driver version (`glxinfo -B | head`), OS, display resolution and
  refresh rate.
- **Build**: compiler, the three commit hashes, presets built, the ctest summary.
- **Section 4** checklist with every deviation.
- **Section 5** tables and the JSON files.
- **Section 6** screenshots.
- **Section 7** renderer notes.
- Any warning CNA prints at start-up.

## 10. Reporting a bug found here

Open it with: the exact command line, the spawn or route, the clock and weather, the camera,
what you expected, what happened, and a screenshot or the overlay. If it is reproducible from a
route, say which -- a defect that a `--route` run reproduces can be turned into a regression
test in `tests/Traffic/RouteDriverTests.cpp` immediately.

---

## Results recorded so far

| date | machine | renderer | what was run | where |
| --- | --- | --- | --- | --- |
| 2026-09-15 | container, 4-core x86-64, **no GPU** (Mesa llvmpipe) | OPENGLES3, OPENGL33, SOFTWARE | build, tests, benchmark suite, screenshots | `docs/performance.md`, `docs/renderer-conformance.md` |

**No run on real GPU hardware has been recorded yet.** When one is, add a row above and put the
numbers in `docs/performance.md` under a heading that names the machine.
