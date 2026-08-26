#!/bin/bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Turn a screen recording into a looping README gif.
#   ./convert.sh ~/Videos/Screencasts/scrolling.mp4
# Produces scrolling.gif next to this script. Override the defaults with
# env vars, e.g. WIDTH=800 FPS=12 ./convert.sh input.mp4
set -euo pipefail

INPUT="$1"
OUTPUT="$(dirname "$0")/$(basename "${INPUT%.*}").gif"
FPS="${FPS:-10}"
WIDTH="${WIDTH:-720}"
COLORS="${COLORS:-96}"

ffmpeg -y -i "${INPUT}" \
        -vf "fps=${FPS},scale=${WIDTH}:-2:flags=lanczos,split[s0][s1];[s0]palettegen=stats_mode=diff:max_colors=${COLORS}[p];[s1][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
        -loop 0 \
        "${OUTPUT}"

# Optional extra squeeze if gifsicle is installed.
if command -v gifsicle >/dev/null 2>&1; then
        gifsicle --optimize=3 --batch "${OUTPUT}"
fi

ls -lh "${OUTPUT}"
