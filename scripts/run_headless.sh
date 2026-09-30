#!/usr/bin/env sh
# Runs the simulator without a physical display (CI, containers): starts a private Xvfb if no
# DISPLAY is set, forwards all arguments to the simulator binary, and stops the Xvfb afterwards.
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
    # A private server on a free display number, stopped when this script ends: a fixed :99
    # collides with anything else on the machine using that display, and a server left behind
    # outlives every run.
    displayFile=$(mktemp)
    Xvfb -displayfd 3 -screen 0 1280x720x24 -nolisten tcp 3>"$displayFile" >/dev/null 2>&1 &
    xvfbPid=$!
    trap 'kill "$xvfbPid" 2>/dev/null; rm -f "$displayFile"' EXIT INT TERM
    tries=0
    while [ ! -s "$displayFile" ] && [ "$tries" -lt 100 ]; do
        sleep 0.1
        tries=$((tries + 1))
    done
    if [ ! -s "$displayFile" ]; then
        echo "error: Xvfb did not report a display" >&2
        exit 1
    fi
    export DISPLAY=":$(cat "$displayFile")"
    "$BIN" "$@"
    exit $?
fi
exec "$BIN" "$@"
