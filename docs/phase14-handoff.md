# Phase 14 handoff (2026-09-26)

Phase 14 is at formal closure. The pushed `b6e89409d7c3cdf922dd82d986bfc8126ed3153f`
source passed a fresh-clone audit; the last pushed documentation SHA still needs
its own fresh-clone audit under `P14-062`. The [task ledger](../plan.md) records
the accepted criteria and remaining gate.

## Accepted work

- `P14-020`–`023`: Czech shop and house frontage variants, an authored square
  walk, forest ground and winter trees, road shoulders and snow transitions,
  cockpit material and control separation in seven time/weather cases, and
  pedestrian gait, clothing, collision and walking-camera review. The
  [curated before/after index](screenshots/phase14/README.md) links the fixed
  views and four-renderer captures.
- `P14-030`–`031`: junction and crossing restrictions, mapped B 21a/B 21b
  semantics, crest sight-distance checks and complete/abort passing decisions.
  Deterministic tests and the simulated 30-minute traffic soak passed. The
  traffic scope is closed for this phase.
- `P14-040`–`042`: listener-selected CC0 Honda Civic startup/idle and Mini Cooper S
  load layers, accepted mixer previews for engine, road, weather, traffic,
  cabin and helicopter, plus weather and visibility effects. See
  [audio design](audio-design.md), [source provenance](../assets/ASSETS.md)
  and the [accepted Mini mixer pack](audio-previews/phase14-mini/README.md).
- `P14-050`–`052`: controlled Radeon 780M scene and mirror measurements before
  the narrow wing-mirror visibility cull; four-renderer conformance after each
  rendering change and a final common scene count. See
  [performance](performance.md) and [renderer conformance](renderer-conformance.md).

## Audit evidence and limits

The current source was reconfigured and built in the existing `build/opengles3`
tree with the shared ccache. The final-source
[`ctest --preset opengles3` run](performance-data/p14-final-ctest.txt) passed all
six registrations in 148.14 seconds on 2026-09-26: unit/scenario/traffic soak,
XNA-only API, virtual-display smoke, map validation, byte-for-byte map
regeneration and asset manifest. A prior project-only ASan/UBSan run passed all
213 core tests, including the three long traffic soaks, in 640.35 seconds with
leak detection enabled; see [sanitizer record](sanitizers.md).

The current four-renderer square check used isolated Xvfb displays at 800 × 480.
Each path reported 620 main-view submissions / 787,905 triangles and 690 indexed
3D submissions / 879,276.7 mean triangles; the two OpenGL images are
byte-identical. The [four JSON records](performance-data/) are named
`p14-final-renderer-*.json`. These Xvfb timings are not a GPU comparison.

Radeon measurements used a hidden SDL offscreen surface with surfaceless EGL,
confirmed by `radeonsi` in the run logs. Mirror visibility culling removed about
162 indexed 3D submissions and 388,000 triangles in the matched rainy-night
cockpit without changing the image. Run-to-run timing spread prevents a
whole-frame speed-up claim. The public-XNA Vulkan snow-terrain representation
adds 52.9 MiB; measured memory pressure did not justify replacing it.

The later runtime audit removed an unnecessary moon-triggered sun-shadow bake
from the rainy-night cockpit. A separate targeted daylight swap keeps the
completed 13:00 image pixel-identical while reducing the matched maximum
project update from 151.145 to 35.726 ms. It adds 29,756 KiB to peak RSS in
that pair, so [the paired timing, geometry, memory and screenshots](performance.md)
are retained together. All four virtual renderers completed the daylight bake
through public XNA calls; Vulkan's virtual-display timing remains variable.

## Fresh-clone audit of `b6e8940`

A genuinely fresh clone from pushed `main` was checked against exact SHA
`b6e89409d7c3cdf922dd82d986bfc8126ed3153f`. It configured the OPENGLES3
Release preset from scratch with the shared `/rv/cnaccache` cache, `CCACHE_BASEDIR=/rv`,
both CMake compiler launchers set to `ccache`, and the existing shared SDL
prebuilt dependency. The complete build succeeded. With `DISPLAY` and
`WAYLAND_DISPLAY` unset, `ctest --preset opengles3 --output-on-failure` passed
all six registrations in 156.15 seconds: unit/scenario and all long traffic
soaks, public-XNA API check, isolated-display smoke, map validation,
byte-for-byte map regeneration, and asset manifest. The
[complete CTest transcript](performance-data/p14-b6-fresh-ctest.txt) is retained.

The same new build ran five 800 × 480 SDL offscreen scenes with surfaceless EGL,
dummy audio and `DISPLAY` unset. Every log reported `driver radeonsi`, confirming
Radeon 780M rather than llvmpipe. Clear square, snow verge and active rainy-night
cockpit screenshots were byte-identical to the committed
[square](screenshots/phase14/square-promenade-after-clear.png),
[snow](screenshots/phase14/verge-phase-after-snow.png) and
[rainy-night](screenshots/phase14/audit-rainy-drive-after.png) references.
The 40-frame walking and helicopter captures completed and were visually
inspected; the cockpit log also reported an active 44.1 kHz stereo audio stream
on the dummy device. This closes `P14-060`; the separate sanitizer result above
was run in the desktop process environment because the restricted shell could
not support LeakSanitizer thread inspection.

`P14-003`, `P14-010`, `P14-011` and `P14-012` also meet their Phase 14 acceptance
criteria. Additional extraction of save/benchmark coordination, the traffic
per-vehicle update path, or broader vehicle/road renderer ownership is deferred
to a future phase; none is required by this closure audit. The remaining gate is
to repeat the fresh-clone build, all checks and representative hidden-Radeon
scenes from the exact final pushed documentation SHA (`P14-062`).
