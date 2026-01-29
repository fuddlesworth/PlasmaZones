#!/bin/bash
# Adapted from https://gist.github.com/Potherca/18423260e2c9a4324c9ecb0c0a284066

INPUT="$1"
OUTPUT="$(basename "${INPUT%.*}")"
#ffmpeg -i "${INPUT}" \
#        -vf "scale=800:-2" \
#        "${OUTPUT}-scaled.webm"

#ffmpeg -i "${OUTPUT}.webm" \
#        -c:v "libx264" \
#        -c:a aac \
#        -movflags faststart \
#        "${OUTPUT}.mp4"

ffmpeg -i "${INPUT}" \
        -vf "fps=10,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" \
        -loop -1 \
        "${OUTPUT}-full.gif"
gifsicle --optimize=3 --output "${OUTPUT}.gif" "${OUTPUT}-full.gif"
