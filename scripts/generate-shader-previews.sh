#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate preview.png for every bundled shader pack under data/overlays/.
#
# Drives plasmazones-shader-render once per pack, renders 60 frames at
# 960x540 with the master-stack layout and sine audio mock, then keeps the
# last frame (iTime ≈ 2s — past any boot-up transient) as
# data/overlays/<id>/preview.png. The shader registry auto-detects this file
# (libs/phosphor-shaders/src/shaderregistry.cpp:251), so no metadata.json
# changes are needed.
#
# Requires: plasmazones-shader-render built (-DBUILD_TOOLS=ON).

set -euo pipefail

# Render settings — locked in with the user 2026-05-06. See README at
# tools/shader-render/README.md for what each flag does.
readonly RESOLUTION="960x540"
readonly FRAMES=60
readonly FPS=30
readonly AUDIO_MODE="sine"
readonly LAYOUT="master-stack"
# Pin zone 1 (the master / hero zone in master-stack) as the only
# highlighted zone for thumbnail captures so every preview features the
# active state on the canonical lead zone. 0 would keep the cycling
# demo schedule documented in renderer.cpp::applyHighlightSchedule().
readonly STILL_HIGHLIGHT_ZONE=1
# Last frame of the sequence: iTime has had time to advance into a
# representative pose for time-driven and audio-reactive shaders.
# Derived from $FRAMES so the two stay in sync if FRAMES is retuned.
# encoder.cpp:18-21 documents the 6-digit zero-padded index format.
readonly KEEP_FRAME="$(printf 'preview_%06d.png' "$FRAMES")"

repo_root() {
    git -C "$(dirname "$0")" rev-parse --show-toplevel
}

REPO_ROOT="$(repo_root)"
SHADER_DIR="$REPO_ROOT/data/overlays"
LAYOUT_DIR="$REPO_ROOT/data/layouts"
RENDERER=""
ONLY=()
DRY_RUN=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [--renderer PATH] [--only ID]... [--dry-run]

Options:
    --renderer PATH   Path to plasmazones-shader-render (auto-detected from
                      build/ if omitted)
    --only ID         Only regenerate this pack (repeatable; defaults to all)
    --dry-run         Print the commands that would run, don't execute
    -h, --help        Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --renderer)
            RENDERER="${2:?--renderer requires a path}"
            shift 2
            ;;
        --only)
            ONLY+=("${2:?--only requires a shader id}")
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

# Auto-detect the renderer if the user didn't pass --renderer. Looks at the
# common CMake binary locations; anything else, the user can point us at it.
if [[ -z "$RENDERER" ]]; then
    for candidate in \
        "$REPO_ROOT/build/bin/plasmazones-shader-render" \
        "$REPO_ROOT/build/tools/shader-render/plasmazones-shader-render"
    do
        if [[ -x "$candidate" ]]; then
            RENDERER="$candidate"
            break
        fi
    done
fi

if [[ -z "$RENDERER" || ! -x "$RENDERER" ]]; then
    echo "Error: plasmazones-shader-render not found." >&2
    echo "       Build it with: cmake -B build -DBUILD_TOOLS=ON && cmake --build build --target plasmazones-shader-render" >&2
    echo "       Or pass --renderer PATH." >&2
    exit 1
fi

# Determine which packs to render. ls -d glob excludes the 'shared/' helper
# directory naturally because it has no metadata.json, but we filter
# explicitly below for clarity and to skip any malformed dirs.
collect_packs() {
    local pack
    for pack in "$SHADER_DIR"/*/; do
        local id
        id="$(basename "$pack")"
        [[ "$id" == "shared" ]] && continue
        [[ -f "$pack/metadata.json" ]] || continue
        echo "$id"
    done
}

if [[ ${#ONLY[@]} -gt 0 ]]; then
    PACKS=("${ONLY[@]}")
else
    mapfile -t PACKS < <(collect_packs)
fi

if [[ ${#PACKS[@]} -eq 0 ]]; then
    echo "Error: no shader packs found under $SHADER_DIR" >&2
    exit 1
fi

WORK_DIR="$(mktemp -d -t plasmazones-shader-previews-XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "Renderer: $RENDERER"
echo "Packs:    ${#PACKS[@]} (${PACKS[*]})"
echo "Settings: ${RESOLUTION} @ ${FPS}fps, ${FRAMES} frames, audio=${AUDIO_MODE}, layout=${LAYOUT}"
echo "Workdir:  $WORK_DIR"
echo

failed=()
for id in "${PACKS[@]}"; do
    pack_dir="$SHADER_DIR/$id"
    if [[ ! -f "$pack_dir/metadata.json" ]]; then
        echo "[skip] $id — no metadata.json under $pack_dir" >&2
        failed+=("$id")
        continue
    fi

    out_dir="$WORK_DIR/$id"
    mkdir -p "$out_dir"

    cmd=(
        "$RENDERER"
        --shader "$id"
        --layout "$LAYOUT"
        --shader-dir "$SHADER_DIR"
        --layout-dir "$LAYOUT_DIR"
        --resolution "$RESOLUTION"
        --frames "$FRAMES"
        --fps "$FPS"
        --audio-mode "$AUDIO_MODE"
        --still-highlight "$STILL_HIGHLIGHT_ZONE"
        --out "$out_dir/preview.png"
    )

    echo "[render] $id"
    if (( DRY_RUN )); then
        printf '           %q ' "${cmd[@]}"
        printf '\n'
        continue
    fi

    if ! "${cmd[@]}"; then
        echo "[fail]   $id — renderer exited non-zero" >&2
        failed+=("$id")
        continue
    fi

    src_frame="$out_dir/$KEEP_FRAME"
    if [[ ! -f "$src_frame" ]]; then
        echo "[fail]   $id — expected $KEEP_FRAME not produced" >&2
        failed+=("$id")
        continue
    fi

    # mv (not cp) so the temp dir cleanup at exit doesn't leave duplicate
    # bytes around; ownership ends up on the repo file.
    mv "$src_frame" "$pack_dir/preview.png"
    echo "[ok]     $id → $pack_dir/preview.png"
done

echo
if (( ${#failed[@]} > 0 )); then
    echo "Done with errors. Failed packs: ${failed[*]}" >&2
    exit 1
fi

echo "Done. Generated previews for ${#PACKS[@]} pack(s)."
