#!/usr/bin/env bash
# Fixed cockpit visual review on the Radeon without opening a desktop window.
# The SDL offscreen EGL surface is 800x480; match that size to avoid clipped frames.
set -euo pipefail

bin="${1:-build/opengles3/bin/cna-car-simulator}"
out="${2:-build/captures/p14-cockpit}"
cases="${3:-noon sunset night rain fog snow rainy_night}"
mkdir -p "$out"

for scene in $cases; do
    case "$scene" in
        noon)        extra=(--time 13:00 --weather clear) ;;
        sunset)      extra=(--time 20:30 --weather clear) ;;
        night)       extra=(--time 22:30 --weather clear --lights) ;;
        rain)        extra=(--time 13:00 --weather rain) ;;
        fog)         extra=(--time 13:00 --weather fog) ;;
        snow)        extra=(--time 13:00 --weather snow) ;;
        rainy_night) extra=(--time 22:30 --weather rain --lights) ;;
        *) echo "unknown cockpit scene: $scene" >&2; exit 2 ;;
    esac
    echo "capturing $scene"
    env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen EGL_PLATFORM=surfaceless \
        EGL_LOG_LEVEL=debug SDL_AUDIODRIVER=dummy "$bin" --no-save --no-audio --lockstep \
        --route town --route-stay --frames 2 --traffic-warmup 60 --time-scale 0 \
        --cockpit --width 800 --height 480 --quality high \
        --screenshot "$out/$scene.png" "${extra[@]}" > "$out/$scene.log" 2>&1
    if ! grep -q 'driver radeonsi' "$out/$scene.log"; then
        echo "$scene did not use the Radeon radeonsi driver; discard this capture" >&2
        exit 1
    fi
    python3 - "$out/$scene.png" <<'PY'
from PIL import Image
import sys

image = Image.open(sys.argv[1]).convert('RGB')
if image.size != (800, 480) or image.getbbox() != (0, 0, 800, 480):
    raise SystemExit(f'clipped or incorrectly sized offscreen capture: {sys.argv[1]}')
PY
done
