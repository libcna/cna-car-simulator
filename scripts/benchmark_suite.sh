#!/usr/bin/env sh
# Runs the repeatable benchmark scenarios and writes one JSON per scene.
#
#   scripts/benchmark_suite.sh [options]
#     --bin PATH        simulator binary (default: build/opengles3/bin/cna-car-simulator)
#     --out DIR         where the JSON goes (default: build/benchmarks)
#     --label NAME      what this run is called in the report (default: the host name)
#     --width  N        viewport width  (default 1280)
#     --height N        viewport height (default 720)
#     --frames N        frames per scene (default 900 = 15 s of simulated time)
#     --route NAME      route to drive (default: town)
#     --scenes "a b"    subset of scene names to run
#     --quick           320x200, 240 frames -- for a software rasteriser or a smoke check
#
# Every scene is deterministic: the same route from the same spawn, the same traffic seed and
# warm-up, a frozen clock and a fixed weather preset, one simulation step per drawn frame. Two
# runs of the same scene on the same machine differ only by measurement noise, so the numbers can
# be compared before and after a change -- which is the only reason this exists.
#
# The scenes span the cases that cost differently: day and night, dry and wet, and the two
# cameras (the cockpit adds the instrument cluster and the rear-view mirror pass).
set -eu

BIN="build/opengles3/bin/cna-car-simulator"
OUT="build/benchmarks"
LABEL="$(hostname 2>/dev/null || echo unknown)"
WIDTH=1280
HEIGHT=720
FRAMES=900
ROUTE="town"
SCENES=""
HERE="$(dirname "$0")"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --bin) BIN="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --label) LABEL="$2"; shift 2;;
        --width) WIDTH="$2"; shift 2;;
        --height) HEIGHT="$2"; shift 2;;
        --frames) FRAMES="$2"; shift 2;;
        --route) ROUTE="$2"; shift 2;;
        --scenes) SCENES="$2"; shift 2;;
        --quick) WIDTH=320; HEIGHT=200; FRAMES=240; shift;;
        -h|--help) sed -n '2,20p' "$0"; exit 0;;
        *) echo "unknown option: $1" >&2; exit 2;;
    esac
done

# name | time | weather | extra arguments
scene_args() {
    case "$1" in
        day_chase)    echo "13:00 clear" ;;
        day_cockpit)  echo "13:00 clear --cockpit" ;;
        rain_chase)   echo "13:00 rain" ;;
        rain_cockpit) echo "13:00 rain --cockpit" ;;
        night_chase)  echo "22:30 clear --lights" ;;
        night_cockpit) echo "22:30 clear --lights --cockpit" ;;
        rainynight_chase) echo "22:30 rain --lights" ;;
        rainynight_cockpit) echo "22:30 rain --lights --cockpit" ;;
        *) echo "" ;;
    esac
}

ALL="day_chase day_cockpit rain_chase rain_cockpit night_chase night_cockpit rainynight_chase rainynight_cockpit"
[ -n "$SCENES" ] || SCENES="$ALL"

mkdir -p "$OUT"
echo "benchmark suite: $LABEL, ${WIDTH}x${HEIGHT}, $FRAMES frames, route $ROUTE"
echo "binary: $BIN"

for scene in $SCENES; do
    args="$(scene_args "$scene")"
    if [ -z "$args" ]; then
        echo "unknown scene: $scene (known: $ALL)" >&2
        exit 2
    fi
    # shellcheck disable=SC2086
    set -- $args
    time_of_day="$1"; weather="$2"; shift 2
    echo "--- $scene ($time_of_day, $weather)"
    "$HERE/run_headless.sh" "$BIN" --no-save --no-audio --lockstep \
        --route "$ROUTE" --route-stay --frames "$FRAMES" --traffic-warmup 60 \
        --time "$time_of_day" --time-scale 0 --weather "$weather" \
        --width "$WIDTH" --height "$HEIGHT" \
        --benchmark --benchmark-json "$OUT/$scene.json" "$@" 2>&1 \
        | grep -E '^(benchmark|route|  (update|draw|frame|scene|passes|visible|traffic))' || true
done

echo
echo "wrote $(ls "$OUT"/*.json 2>/dev/null | wc -l) scene file(s) to $OUT"
echo "report: python3 scripts/benchmark_report.py $OUT --label \"$LABEL\""
