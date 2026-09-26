# Phase 14 handoff (2026-09-26)

Phase 14 remains in progress. The final fresh-clone audit (`P14-062`) has not
started. The [task ledger](../plan.md) records individual acceptance checks;
this page points to the compact evidence needed for the remaining audit.

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

The clean from-scratch build portion of `P14-060` is deferred to `P14-062` so it
can share the required final fresh-clone audit. `../AGENTS.md` directs routine
work to reuse the existing build tree and shared cache. No fresh clone has been
created for Phase 14's final audit yet. Before `P14-062`, reconcile this handoff,
the task ledger and the final pushed HEAD, then perform the clone, build, checks
and representative scenes from that exact SHA.
