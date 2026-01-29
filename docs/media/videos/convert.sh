#!/bin/bash
# Adapted from https://gist.github.com/Potherca/18423260e2c9a4324c9ecb0c0a284066

INPUT="$1"
OUTPUT="$(basename "${INPUT%.*}")"
ffmpeg -i "${INPUT}" \
        -vf "scale=800:-2" \
        "${OUTPUT}-scaled.webm"
