#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate a PlasmaZones support report and copy to clipboard.
# Usage: plasmazones-support-report.sh [--since MINUTES] [--no-copy]
#
# Requires: plasmazonesd running, qdbus6 or busctl

set -euo pipefail

SINCE_MINUTES=30
COPY=true

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
        --no-copy)
            COPY=false
            shift
            ;;
        -h|--help)
            echo "Usage: $(basename "$0") [--since MINUTES] [--no-copy]"
            echo ""
            echo "Generate a PlasmaZones support report for bug reports/discussions."
            echo ""
            echo "Options:"
            echo "  --since MINUTES  Minutes of journal logs to include (default: 30, max: 120)"
            echo "  --no-copy        Print to stdout instead of copying to clipboard"
            echo "  -h, --help       Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Try qdbus6 first (KDE default), then qdbus, then busctl
call_dbus() {
    if command -v qdbus6 &>/dev/null; then
        qdbus6 org.plasmazones.daemon /Control org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v qdbus &>/dev/null; then
        qdbus org.plasmazones.daemon /Control org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v busctl &>/dev/null; then
        busctl --user --json=short call org.plasmazones.daemon /Control org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>/dev/null \
            | python3 -c "import sys,json; print(json.load(sys.stdin)['data'][0])" 2>/dev/null \
            || busctl --user call org.plasmazones.daemon /Control org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" | sed 's/^s "//' | sed 's/"$//'
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

if $COPY; then
    if command -v wl-copy &>/dev/null; then
        echo "$REPORT" | wl-copy
        echo "Support report copied to clipboard ($(echo "$REPORT" | wc -l) lines)."
        echo "Paste it into a GitHub Discussion or Issue."
    elif command -v xclip &>/dev/null; then
        echo "$REPORT" | xclip -selection clipboard
        echo "Support report copied to clipboard ($(echo "$REPORT" | wc -l) lines)."
        echo "Paste it into a GitHub Discussion or Issue."
    else
        echo "No clipboard tool found (wl-copy or xclip). Printing to stdout:" >&2
        echo ""
        echo "$REPORT"
    fi
else
    echo "$REPORT"
fi
