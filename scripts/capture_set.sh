#!/usr/bin/env sh
# Captures the curated screenshot set (the pictures in README.md and docs/screenshots/).
#
#   scripts/capture_set.sh [binary] [output directory]
#
# Defaults: build/opengles3/bin/cna-car-simulator and docs/screenshots/.
# Runs headless through scripts/run_headless.sh, so it works over Xvfb in a container.
# Every run is deterministic (--lockstep, fixed spawns and frame counts), so the pictures can
# be compared between builds and between renderers.
set -eu
BIN="${1:-build/opengles3/bin/cna-car-simulator}"
OUT="${2:-docs/screenshots}"
HERE="$(dirname "$0")"
mkdir -p "$OUT"

shot() {
    name="$1"; shift
    echo "capturing $name"
    "$HERE/run_headless.sh" "$BIN" --no-save --no-audio --lockstep "$@" --screenshot "$OUT/$name.png" 2>&1 \
        | grep -vE '^\[INFO\]' | tail -2
}

# Hero: three-quarter rear of the parked car at the town spawn.
shot hero        --spawn square --frames 30  --chase-yaw 40 --chase-distance 5 --traffic-warmup 20
# Cockpit and the instrument cluster, driving at about 35 km/h with traffic ahead.
shot cockpit     --spawn square --frames 480 --auto-drive 8  --traffic-warmup 40 --cockpit \
                 --screenshot-cluster "$OUT/cluster.png"
# Town street, oncoming traffic, the square, the countryside, the forest.
shot town        --spawn square --frames 480 --auto-drive 8  --traffic-warmup 40
shot traffic     --spawn square --frames 480 --auto-drive 10 --traffic-warmup 60
shot square      --spawn square --frames 40  --view -78 9 -30 50 -6
shot countryside --spawn fields --frames 150 --auto-drive 6
shot forest      --spawn forest --frames 900 --auto-drive 15
# Headlights on (the flag starts the engine itself so the electrics are live).
shot lights      --spawn square --frames 240 --chase-yaw 150 --chase-distance 5 --lights --traffic-warmup 20
# Time of day: the same square at dusk and at night, and a lit street from beside the car.
shot dusk        --spawn square --frames 40  --view -78 9 -30 50 -6 --time 21:30 --time-scale 0
shot night       --spawn square --frames 40  --view -66 6 25 0 -10  --time 23:00 --time-scale 0
shot headlights  --spawn square --frames 300 --chase-yaw 90 --chase-distance 14 --lights --auto-drive 4 \
                 --time 23:00 --time-scale 0 --traffic-warmup 20
# Weather: the same square under a lid and in the rain.
shot overcast    --spawn square --frames 40  --view -78 9 -30 50 -6 --time 13:00 --time-scale 0 --weather overcast
shot rain        --spawn square --frames 40  --view -78 9 -30 50 -6 --time 13:00 --time-scale 0 --weather rain

echo
echo "captured into $OUT; convert the scene shots to JPEG before committing them:"
echo "  python3 -c \"from PIL import Image; [Image.open(f'$OUT/{n}.png').convert('RGB')"
echo "      .save(f'$OUT/{n}.jpg', quality=88, optimize=True, subsampling=1) for n in"
echo "      ['hero','cockpit','town','traffic','square','countryside','forest','lights',"
echo "      'dusk','night','headlights','overcast','rain']]\""
