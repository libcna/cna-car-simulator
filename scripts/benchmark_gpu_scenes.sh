#!/usr/bin/env bash
# Fixed Phase 14 Radeon scenes using hidden SDL offscreen and surfaceless EGL.
# The initial SDL offscreen EGL surface is 800x480; larger logical buffers are clipped.
# Each scene writes benchmark JSON, a final-frame PNG, process RSS and the full simulator log.
set -euo pipefail

bin="${1:-build/opengles3/bin/cna-car-simulator}"
out="${2:-build/benchmarks/p14-gpu-scenes}"
frames="${3:-120}"
scenes="${4:-town_clear town_cockpit forest forest_snow town_fog square_people walking flight}"
mkdir -p "$out"

for scene in $scenes; do
    case "$scene" in
        town_clear)   extra=(--route town --route-stay --time 13:00 --weather clear); expected=chase ;;
        town_cockpit) extra=(--route town --route-stay --time 13:00 --weather clear --cockpit); expected=cockpit ;;
        forest)       extra=(--spawn forest --time 13:00 --weather clear --view -228 5 -1280 0 -4); expected=free ;;
        forest_snow)  extra=(--spawn forest --time 13:00 --weather snow --view -228 5 -1280 0 -4); expected=free ;;
        town_fog)     extra=(--spawn square --time 13:00 --weather fog --view -78 9 -30 50 -6); expected=free ;;
        square_people) extra=(--spawn square --time 13:00 --weather clear --view -61 6.5 14 0 -4); expected=free ;;
        walking)      extra=(--spawn square --time 13:00 --weather clear --walk); expected=walking ;;
        flight)       extra=(--spawn square --time 13:00 --weather clear --flight --view -125 95 150 0 -30); expected=flight ;;
        *) echo "unknown scene: $scene" >&2; exit 2 ;;
    esac
    echo "running $scene"
    env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen EGL_PLATFORM=surfaceless \
        EGL_LOG_LEVEL=debug SDL_AUDIODRIVER=dummy "$bin" --no-save --no-audio --lockstep --time-scale 0 \
        --frames "$frames" --traffic-warmup 60 --width 800 --height 480 --quality high \
        --benchmark --benchmark-json "$out/$scene.json" --screenshot "$out/$scene.png" \
        "${extra[@]}" > "$out/$scene.log" 2>&1 &
    pid=$!
    peak_rss_kib=0
    while kill -0 "$pid" 2>/dev/null; do
        if [[ -r "/proc/$pid/status" ]]; then
            rss_kib="$(awk '$1 == "VmRSS:" {print $2}' "/proc/$pid/status")"
            if [[ -n "$rss_kib" ]] && (( rss_kib > peak_rss_kib )); then
                peak_rss_kib=$rss_kib
            fi
        fi
        sleep 0.25
    done
    wait "$pid"
    printf 'peakRssKiB=%s\n' "$peak_rss_kib" > "$out/$scene.process"
    if ! grep -q 'driver radeonsi' "$out/$scene.log"; then
        echo "$scene did not use the Radeon radeonsi driver; discard this run" >&2
        exit 1
    fi
    if ! grep -Eq "^benchmark: .* scene .* $expected( |$)" "$out/$scene.log"; then
        echo "$scene did not finish in the expected $expected camera; discard this run" >&2
        exit 1
    fi
    if ! grep -q '800x480, scene ' "$out/$scene.log"; then
        echo "$scene did not retain the 800x480 physical offscreen size; discard this run" >&2
        exit 1
    fi
    grep -E '^(benchmark:|  (update|draw|frame|scene|passes|visible|traffic|people))' "$out/$scene.log"
done
