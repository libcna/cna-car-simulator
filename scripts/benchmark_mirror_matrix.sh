#!/usr/bin/env bash
# Hidden Radeon mirror isolation using SDL offscreen and surfaceless EGL.
# CNA's initial SDL offscreen EGL surface is 800x480; larger logical buffers would be
# clipped to that surface, so keep the physical and logical sizes matched here.
# JSON, a final-frame image, a log and peak process RSS are saved for each variant.
set -euo pipefail

bin="${1:-build/opengles3/bin/cna-car-simulator}"
out="${2:-build/benchmarks/p14-mirror}"
frames="${3:-120}"
variants="${4:-all none rear rear384 rear192 rear_every2 rear150 rear75}"
mkdir -p "$out"
declare -A runs=()

for variant in $variants; do
    runs[$variant]=$(( ${runs[$variant]:-0} + 1 ))
    name="$variant"
    if (( runs[$variant] > 1 )); then name="$variant-r${runs[$variant]}"; fi
    case "$variant" in
        all)          extra=() ;;
        none)         extra=(--no-mirror) ;;
        rear)         extra=(--no-wing-mirrors) ;;
        rear384)      extra=(--no-wing-mirrors --mirror-width 384) ;;
        rear192)      extra=(--no-wing-mirrors --mirror-width 192) ;;
        rear_every2)  extra=(--no-wing-mirrors --mirror-every 2) ;;
        rear150)      extra=(--no-wing-mirrors --mirror-distance 150) ;;
        rear75)       extra=(--no-wing-mirrors --mirror-distance 75) ;;
        *) echo "unknown variant: $variant" >&2; exit 2 ;;
    esac
    echo "running $name"
    env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen EGL_PLATFORM=surfaceless \
        EGL_LOG_LEVEL=debug SDL_AUDIODRIVER=dummy "$bin" --no-save --no-audio --lockstep \
        --route town --route-stay --frames "$frames" --traffic-warmup 60 \
        --time 22:30 --time-scale 0 --weather rain --lights --cockpit \
        --width 800 --height 480 --quality high --benchmark \
        --benchmark-json "$out/$name.json" --screenshot "$out/$name.png" \
        "${extra[@]}" > "$out/$name.log" 2>&1 &
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
    printf 'peakRssKiB=%s\n' "$peak_rss_kib" > "$out/$name.process"
    if ! grep -q 'driver radeonsi' "$out/$name.log"; then
        echo "$name did not use the Radeon radeonsi driver; discard this run" >&2
        exit 1
    fi
    if ! grep -q 'scene 22:30 rain cockpit$' "$out/$name.log"; then
        echo "$name did not finish in the intended cockpit scene; discard this run" >&2
        exit 1
    fi
    if ! grep -q '800x480, scene ' "$out/$name.log"; then
        echo "$name did not retain the 800x480 physical offscreen size; discard this run" >&2
        exit 1
    fi
    grep -E '^(benchmark:|  (update|draw|frame|scene|passes|visible|traffic|people))' "$out/$name.log"
done
