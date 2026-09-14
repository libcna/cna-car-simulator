# Real-hardware validation procedure

Everything in this repository was developed and verified in a headless container (Xvfb, Mesa
llvmpipe software OpenGL). The pictures and performance numbers therefore describe the software
rasteriser, not a GPU. This page is the reproducible procedure for a first run on a real PC with
a display, a GPU and a sound device, and the list of what to record so the results can be
compared with the container baseline in `docs/performance.md` and `docs/renderer-conformance.md`.

## 1. Build

Follow `README.md`, "Building" (sibling checkouts of CNA `next`, Sharp Runtime `next`,
`easy-gl`, `meta-gl`). On Linux with a GPU driver:

```bash
cmake --preset opengles3 && cmake --build --preset opengles3 -j && ctest --preset opengles3
cmake --preset opengl33  && cmake --build --preset opengl33  -j     # second renderer to compare
```

On Windows use the `default` preset (CNA picks its platform default) or `opengl33`. Record:
compiler and version, CNA and Sharp Runtime commit hashes (`git -C ../cna rev-parse HEAD`),
`ctest` summary (all tests must pass; the headless smoke tests need a display or Xvfb).

## 2. Launch and sanity

```bash
./build/opengles3/bin/cna-car-simulator --debug-overlay
```

Expected within a few seconds: the Lipová square at the `square` spawn, the exterior chase
camera behind a red Lipan 1.2, the HUD strip at the bottom right (`0 km/h`, rpm, gear), the
debug overlay at the top left with frame times, pass times, draw calls and triangles.
The log on stdout names the renderer (`CNA: graphics renderer: ...`), the map build time and
the world build time; record both times.

Checklist (tick each; note anything that differs):

- [ ] Window opens at 1280 x 720 (or `--width/--height`), `--fullscreen` works, `Esc` quits.
- [ ] `E` cranks and starts the engine; the starter whine, catch and idle are audible; the rpm
      needle settles near 850.
- [ ] Automatic mode (`T` toggles): `F` selects drive, `W` accelerates, `S` brakes, the car
      reaches 50 km/h in town without wheel spin, upshifts are audible (short load dip).
- [ ] Manual mode: `Q` clutch, `Left Shift`/`Left Ctrl` gears; a stall is possible when the
      clutch is dropped at idle.
- [ ] `C` switches to the cockpit: instrument cluster live (speed, rpm, fuel, temperature,
      indicator and beam telltales), steering wheel turns with `A`/`D`, the rear-view mirror
      shows the road and traffic behind, `M` hides it.
- [ ] `,` `.` `H` indicators tick and blink on the cluster and on the car; `L`/`K` headlights
      and high beam light the lamps; `B` horn.
- [ ] Traffic: cars follow lanes, keep distance, yield at the church junction and the stop
      sign, never overlap the player; brake lights and indicators visible.
- [ ] Driving out of town (east on the main road): tree avenue, fields, delineators, village
      plots with fences and poles, the forest track (gravel rolling noise changes).
- [ ] `Backspace` resets the car to the road after leaving it; `F5` resets the trip meter.
- [ ] `F1` help, `F3` debug overlay, `F12` screenshot (PNG in the working directory).
- [ ] Quit with `Esc`; the save file is written (`--save <file>` to choose the location).

## 3. Views to capture

Capture the same set as the curated container set (`docs/screenshots/`) so the pictures line
up side by side. From the `square` spawn, `--lockstep` keeps captures deterministic:

```bash
S="./build/opengles3/bin/cna-car-simulator --no-save --lockstep --frames 120 --auto-drive 6 --traffic-warmup 40"
$S --spawn square --screenshot hw-town.png
$S --spawn square --chase-yaw 35 --chase-distance 5.5 --screenshot hw-hero.png
$S --spawn square --cockpit --screenshot hw-cockpit.png --screenshot-cluster hw-cluster.png
$S --spawn fields --screenshot hw-countryside.png
$S --spawn forest --screenshot hw-forest.png
$S --spawn east   --screenshot hw-intersection.png
$S --spawn square --lights --screenshot hw-lights.png
```

Also capture one interactive frame with `F12` after a few minutes of driving (real traffic
state, real camera motion). Compare against `docs/screenshots/` and note: shading differences
(sun direction, shadow darkness), texture filtering (anisotropic filtering on a GPU sharpens
the road wear tracks that llvmpipe blurs), alpha-tested tree cards (edge halos), fog banding,
z-fighting on road markings or the vehicle ground shadow, missing geometry.

## 4. Metrics to report

Run the benchmark set and attach the JSON files:

```bash
B="./build/opengles3/bin/cna-car-simulator --no-save --no-audio --lockstep --frames 150 --auto-drive 6 --benchmark"
$B --spawn square --traffic-warmup 40 --benchmark-json hw-town-chase.json
$B --spawn square --traffic-warmup 40 --cockpit --benchmark-json hw-town-cockpit.json
$B --spawn square --traffic-warmup 40 --cockpit --mirror-every 2 --benchmark-json hw-town-cockpit-m2.json
$B --spawn forest --benchmark-json hw-forest.json
$B --spawn fields --benchmark-json hw-fields.json
```

Report per run: `frameMsAvg`, `drawMsAvg`, `drawCallsAvg`, `trianglesAvg` and the
`passesMsAvg` block. On a GPU the draw submission should be a few milliseconds and the frame
time should sit at the display's refresh interval (vertical sync is on); if `frameMsAvg` is
well above 16.7 ms note the GPU model and driver. Draw calls and triangles must match the
container numbers exactly for the same scene (the scene is deterministic); a difference points
at a culling bug that depends on floating-point behaviour.

Also record from the debug overlay while driving: audio underruns (must stay 0), the maximum
`Update` time, and whether the frame time stays smooth when the mirror is on.

## 5. Sound

With a real device: engine at idle, load and overrun (burble on a closed throttle downhill),
gear-change dip, tyre noise on asphalt versus the forest gravel, brake hiss, wind above
80 km/h, horn, indicator relay, impact sound on a fence. Note any clicks at block boundaries
(a defect) and the level balance; `Page Up`/`Page Down` sets the master volume.

## 6. What to send back

- Machine: CPU, GPU, driver version, OS, display resolution, refresh rate.
- Build: compiler, CNA/Sharp Runtime/easy-gl/meta-gl commits, presets built, `ctest` result.
- The checklist above with deviations.
- The captures from section 3 and the JSON files from section 4.
- Log excerpts for any warning printed by CNA at start-up (renderer capabilities line).

Open questions this validation answers: the true GPU frame budget (container numbers are
software-rasteriser bound), whether the alpha-tested vegetation and the fog look right with
anisotropic filtering and multisampling, and whether the audio stream stays underrun-free at
real-time rates (the container runs audio with a dummy device).
