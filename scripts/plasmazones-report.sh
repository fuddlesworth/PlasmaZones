#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate a PlasmaZones support report archive.
#
# Collects the daemon's redacted Markdown report, config files, layout files,
# and journal logs into a timestamped .tar.gz archive for attaching to
# GitHub Issues or Discussions.
#
# Requires: plasmazonesd running, qdbus6 or busctl

set -euo pipefail

SINCE_MINUTES=30
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --since)
            SINCE_MINUTES="${2:?--since requires a number of minutes}"
            if ! [[ "$SINCE_MINUTES" =~ ^[0-9]+$ ]] || [[ "$SINCE_MINUTES" -lt 1 ]] || [[ "$SINCE_MINUTES" -gt 120 ]]; then
                echo "Error: --since must be a number between 1 and 120" >&2
                exit 1
            fi
            shift 2
            ;;
        --output)
            OUTPUT_DIR="${2:?--output requires a directory path}"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $(basename "$0") [--since MINUTES] [--output DIR]"
            echo ""
            echo "Generate a PlasmaZones support report archive for bug reports/discussions."
            echo ""
            echo "Options:"
            echo "  --since MINUTES  Minutes of journal logs to include (default: 30, max: 120)"
            echo "  --output DIR     Directory for the archive (default: \$TMPDIR or /tmp)"
            echo "  -h, --help       Show this help"
            echo ""
            echo "The archive contains:"
            echo "  report.md        Redacted Markdown report from the daemon"
            echo "  config.json      Current configuration (home paths redacted)"
            echo "  session.json     Window session state (home paths redacted)"
            echo "  data/            User data (layouts, algorithms, shaders, etc.)"
            echo "  journal.log      Recent plasmazonesd journal entries"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Resolve output directory
if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="${TMPDIR:-/tmp}"
fi
mkdir -p "$OUTPUT_DIR"

# ─── D-Bus call ───────────────────────────────────────────────────────────────

call_dbus() {
    if command -v qdbus6 &>/dev/null; then
        qdbus6 org.plasmazones /PlasmaZones org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v qdbus &>/dev/null; then
        qdbus org.plasmazones /PlasmaZones org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v busctl &>/dev/null; then
        local busctl_err
        busctl_err=$(busctl --user --json=short call org.plasmazones /PlasmaZones org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>&1) \
            && python3 -c "import sys,json; print(json.load(sys.stdin)['data'][0])" <<< "$busctl_err" 2>/dev/null \
            || busctl --user call org.plasmazones /PlasmaZones org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>&1 \
            | sed 's/^s "//' | sed 's/"$//'
    else
        echo "Error: No D-Bus CLI tool found (qdbus6, qdbus, or busctl required)" >&2
        exit 1
    fi
}

REPORT=$(call_dbus 2>/dev/null) || {
    echo "Error: Could not connect to PlasmaZones daemon." >&2
    echo "Make sure plasmazonesd is running." >&2
    exit 1
}

if [[ -z "$REPORT" ]]; then
    echo "Error: Empty report returned from daemon." >&2
    exit 1
fi

# ─── Build archive staging directory ──────────────────────────────────────────

STAGING=$(mktemp -d "${TMPDIR:-/tmp}/plasmazones-report.XXXXXXXXXX")
trap 'rm -rf "$STAGING"' EXIT

# 1. Markdown report from daemon (already redacted)
echo "$REPORT" > "$STAGING/report.md"

# 2. Config file (redact home paths)
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/plasmazones"
# Guard against empty HOME (e.g., running from a systemd service without User=)
if [[ -z "${HOME:-}" ]]; then
    echo "Warning: \$HOME is not set — skipping home path redaction" >&2
    HOME_ESC=""
else
    # Escape all sed metacharacters in HOME for safe use in sed patterns (| delimiter)
    HOME_ESC=$(printf '%s' "$HOME" | sed 's/[][\\.^$*+?{}()|]/\\&/g')
fi
redact_home() {
    if [[ -n "$HOME_ESC" ]]; then
        sed "s|$HOME_ESC|~|g" "$@"
    else
        cat "$@"
    fi
}

if [[ -f "$CONFIG_DIR/config.json" ]]; then
    redact_home "$CONFIG_DIR/config.json" > "$STAGING/config.json"
fi

# 3. Session file (redact home paths — window classes/titles kept for diagnostic value)
if [[ -f "$CONFIG_DIR/session.json" ]]; then
    redact_home "$CONFIG_DIR/session.json" > "$STAGING/session.json"
fi

# 4. User data directory (layouts, custom algorithms, shaders, etc.)
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/plasmazones"
if [[ -d "$DATA_DIR" ]]; then
    mkdir -p "$STAGING/data"
    # Copy tree structure, redacting home paths in text files
    find "$DATA_DIR" -type f | while IFS= read -r f; do
        rel="${f#"$DATA_DIR"/}"
        mkdir -p "$STAGING/data/$(dirname "$rel")"
        case "$f" in
            *.json|*.js|*.glsl|*.frag|*.vert|*.conf|*.txt)
                redact_home "$f" > "$STAGING/data/$rel" ;;
            *)
                cp "$f" "$STAGING/data/$rel" ;;
        esac
    done
fi

# 5. Journal logs
if command -v journalctl &>/dev/null; then
    JOURNAL=$(journalctl --user -t plasmazonesd \
        --since "$SINCE_MINUTES min ago" \
        --no-pager -o short-iso 2>/dev/null \
        | head -n 2000 \
        | redact_home) || true

    # Fallback: try --identifier if -t returned nothing
    if [[ -z "$JOURNAL" ]]; then
        JOURNAL=$(journalctl --user --identifier=plasmazonesd \
            --since "$SINCE_MINUTES min ago" \
            --no-pager -o short-iso 2>/dev/null \
            | head -n 2000 \
            | redact_home) || true
    fi

    if [[ -n "$JOURNAL" ]]; then
        echo "$JOURNAL" > "$STAGING/journal.log"
    fi
fi

# ─── Create archive ──────────────────────────────────────────────────────────

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
ARCHIVE_NAME="plasmazones-report-${TIMESTAMP}.tar.gz"
ARCHIVE_PATH="$OUTPUT_DIR/$ARCHIVE_NAME"

tar -czf "$ARCHIVE_PATH" -C "$STAGING" .

echo "Support report archive created:"
echo "  $ARCHIVE_PATH"
echo ""
echo "Contents:"
tar -tzf "$ARCHIVE_PATH" | sed 's|^./||' | grep -v '^$' | sed 's/^/  /'
echo ""
echo "Attach this file to your GitHub Issue or Discussion."
