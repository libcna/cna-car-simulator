#!/usr/bin/env sh
# Regenerates every bitmap-font atlas under content/fonts from the D-DIN family
# (assets/external/fonts/d-din, SIL OFL 1.1). Requires Python 3 with Pillow.
set -eu
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
FONTS=assets/external/fonts/d-din
"$PY" tools/fontatlas.py --font "$FONTS/D-DIN.ttf"                --size 28  --charset latin  --atlas 512  --out content/fonts/ui_regular_28
"$PY" tools/fontatlas.py --font "$FONTS/D-DIN-Bold.ttf"           --size 44  --charset latin  --atlas 1024 --out content/fonts/ui_bold_44
"$PY" tools/fontatlas.py --font "$FONTS/D-DIN-Bold.ttf"           --size 128 --charset plate  --atlas 1024 --out content/fonts/plate_bold_128
"$PY" tools/fontatlas.py --font "$FONTS/D-DINCondensed-Bold.ttf"  --size 96  --charset digits --atlas 512  --out content/fonts/gauge_condensed_96
echo "fonts regenerated"
