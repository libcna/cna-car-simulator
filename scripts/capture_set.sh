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
# The other settlements of the region.
shot brezi       --spawn brezi    --frames 120 --chase-yaw 35 --chase-distance 12 --traffic-warmup 20
shot podhaji     --spawn podhaji  --frames 120 --chase-yaw 35 --chase-distance 12 --traffic-warmup 20
shot mesto       --spawn mesto    --frames 120 --chase-yaw 35 --chase-distance 12 --traffic-warmup 20
# The signalised junction "U kaple", by day and after dark.
shot signals     --spawn kostel --frames 90 --view 312 7.5 -22 92 -2 --time 13:00 --time-scale 0 --traffic-warmup 20
# Weather: the same square under a lid and in the rain.
shot overcast    --spawn square --frames 40  --view -78 9 -30 50 -6 --time 13:00 --time-scale 0 --weather overcast
shot rain        --spawn square --frames 40  --view -78 9 -30 50 -6 --time 13:00 --time-scale 0 --weather rain
# Wet road and rainy night: the two scenes the wet sheen and the headlamp beam are judged on.
shot wetroad     --spawn kostel --frames 240 --auto-drive 4 --chase-distance 8 --traffic-warmup 30 \
                 --time 13:00 --time-scale 0 --weather rain
shot rainynight  --spawn kostel --frames 240 --auto-drive 4 --chase-distance 8 --traffic-warmup 30 --lights \
                 --time 22:30 --time-scale 0 --weather rain

# The scene pictures are committed as JPEG (the set is 19 frames at 1280 x 720; as PNG it would
# be 25 MB in the repository). The instrument cluster stays PNG because it is a flat UI target
# where JPEG ringing is visible on the dial markings. Converting here rather than printing a
# command for somebody to paste means the list cannot go stale when a scene is added.
echo
echo "converting the scene captures to JPEG"
PY_BIN="$(command -v python3.12 || command -v python3)"
"$PY_BIN" - "$OUT" <<'PYEOF'
import pathlib
import sys

from PIL import Image

out = pathlib.Path(sys.argv[1])
keep_png = {"cluster"}
converted = 0
for png in sorted(out.glob("*.png")):
    if png.stem in keep_png:
        continue
    Image.open(png).convert("RGB").save(out / f"{png.stem}.jpg", quality=88, optimize=True, subsampling=1)
    png.unlink()
    converted += 1
print(f"  {converted} scene picture(s) written as JPEG, cluster.png kept as PNG")
PYEOF

echo "done: $OUT"
