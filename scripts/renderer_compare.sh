#!/usr/bin/env sh
# Runs the same deterministic frame through several renderers and reports what actually happened.
#
#   scripts/renderer_compare.sh [--out DIR] [--scene town|cockpit] [preset ...]
#
# Default presets: opengles3 opengl33 software. Each must already be built
# (cmake --preset <p> && cmake --build build/<p>); a preset that is not built is reported as
# "not built" rather than silently skipped, because "we tested three renderers" has to mean
# something.
#
# For each renderer it records, in the vocabulary docs/renderer-conformance.md uses:
#   configures / compiles  -- the build directory exists and has a binary
#   starts                 -- the process ran and exited cleanly
#   renders                -- a screenshot came out, with its draw calls and triangles
#   performance tested     -- the benchmark numbers below
# *Visual inspection* is not something a script can do: it prints the file names and says so.
set -eu

OUT="build/renderers"
SCENE="town"
PRESETS=""
HERE="$(dirname "$0")"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2;;
        --scene) SCENE="$2"; shift 2;;
        -h|--help) sed -n '2,20p' "$0"; exit 0;;
        *) PRESETS="$PRESETS $1"; shift;;
    esac
done
[ -n "$PRESETS" ] || PRESETS="opengles3 opengl33 software"

case "$SCENE" in
    town)    EXTRA="" ;;
    cockpit) EXTRA="--cockpit" ;;
    *) echo "unknown scene: $SCENE (town or cockpit)" >&2; exit 2;;
esac

mkdir -p "$OUT"
echo "renderer comparison, scene '$SCENE'"
printf '%-12s %-10s %-10s %10s %12s %14s\n' preset built started rendered "draw calls" "draw ms"

for preset in $PRESETS; do
    BIN="build/$preset/bin/cna-car-simulator"
    if [ ! -x "$BIN" ]; then
        printf '%-12s %-10s %-10s %10s %12s %14s\n' "$preset" "not built" "-" "-" "-" "-"
        continue
    fi
    LOG="$OUT/$preset-$SCENE.log"
    # The same simulated state on every renderer: fixed spawn, fixed clock, fixed weather, one
    # simulation step per drawn frame, traffic warmed up by the same number of steps.
    if "$HERE/run_headless.sh" "$BIN" --no-save --no-audio --lockstep --spawn square \
            --frames 40 --auto-drive 3 --traffic-warmup 20 \
            --time 13:00 --time-scale 0 --weather cloudy $EXTRA \
            --benchmark --benchmark-json "$OUT/$preset-$SCENE.json" \
            --screenshot "$OUT/$preset-$SCENE.png" > "$LOG" 2>&1; then
        STARTED=yes
    else
        STARTED=failed
    fi
    RENDERED=no
    [ -f "$OUT/$preset-$SCENE.png" ] && RENDERED=yes
    CALLS=$(python3 -c "import json,sys;print(round(json.load(open('$OUT/$preset-$SCENE.json'))['drawCallsAvg']))" 2>/dev/null || echo "-")
    MS=$(python3 -c "import json,sys;print(round(json.load(open('$OUT/$preset-$SCENE.json'))['drawMsAvg'],1))" 2>/dev/null || echo "-")
    printf '%-12s %-10s %-10s %10s %12s %14s\n' "$preset" yes "$STARTED" "$RENDERED" "$CALLS" "$MS"
done

echo
echo "images in $OUT; visual inspection is a human step -- open them side by side and record what"
echo "differs in docs/renderer-conformance.md. Equal draw calls are not visual equivalence."
