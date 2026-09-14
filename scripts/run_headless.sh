#!/usr/bin/env sh
# Runs the simulator without a physical display (CI, containers): starts Xvfb if no DISPLAY
# is set and forwards all arguments to the simulator binary.
#
#   scripts/run_headless.sh build/opengles3/bin/cna-car-simulator --frames 120 --screenshot out.png
#
# Environment: SDL_AUDIODRIVER defaults to "dummy" so no audio device is required.
set -eu
if [ "$#" -lt 1 ]; then
    echo "usage: $0 <path-to-cna-car-simulator> [simulator arguments...]" >&2
    exit 2
fi
BIN="$1"; shift
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp}"
if [ -z "${DISPLAY:-}" ]; then
    if ! command -v Xvfb >/dev/null 2>&1; then
        echo "error: no DISPLAY and Xvfb is not installed" >&2
        exit 1
    fi
    export DISPLAY=:99
    if ! pgrep -f "Xvfb ${DISPLAY}" >/dev/null 2>&1; then
        Xvfb "${DISPLAY}" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
        sleep 1
    fi
fi
exec "$BIN" "$@"
