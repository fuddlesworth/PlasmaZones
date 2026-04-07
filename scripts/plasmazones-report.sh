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
# The D-Bus report (report.md) already contains config/session/layout summaries,
# but we also include the raw files (config.json, session.json, data/) so that
# triagers can inspect exact JSON without re-serialization artefacts.
#
# Requires: plasmazonesd running, qdbus6 or busctl, perl (with JSON::PP for busctl)

set -euo pipefail

# These defaults mirror src/core/supportreport.cpp — keep in sync.
SINCE_MINUTES=30       # DefaultSinceMinutes
MAX_SINCE_MINUTES=120  # MaxSinceMinutes
MAX_LOG_LINES=2000     # MaxLogLines
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --since)
            SINCE_MINUTES="${2:?--since requires a number of minutes}"
            if ! [[ "$SINCE_MINUTES" =~ ^[0-9]+$ ]] || [[ "$SINCE_MINUTES" -gt "$MAX_SINCE_MINUTES" ]]; then
                echo "Error: --since must be a number between 0 and $MAX_SINCE_MINUTES" >&2
                exit 1
            fi
            # 0 is passed through to the daemon which applies its own default (30 min)
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
            echo "  --since MINUTES  Minutes of journal logs to include (0 = default 30, max: $MAX_SINCE_MINUTES)"
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

# Resolve output directory to an absolute path so the final message
# remains useful regardless of later CWD changes.
if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="${TMPDIR:-/tmp}"
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)

# ─── D-Bus call ───────────────────────────────────────────────────────────────

call_dbus() {
    if command -v qdbus6 &>/dev/null; then
        qdbus6 org.plasmazones /PlasmaZones org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v qdbus &>/dev/null; then
        qdbus org.plasmazones /PlasmaZones org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v busctl &>/dev/null; then
        local raw
        if raw=$(busctl --user --json=short call org.plasmazones /PlasmaZones org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>/dev/null); then
            # Parse busctl JSON output: {"type":"s","data":["..."]}
            perl -MJSON::PP -e 'print decode_json(do{local $/;<STDIN>})->{data}[0]' <<< "$raw"
        else
            raw=$(busctl --user call org.plasmazones /PlasmaZones org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>&1) || {
                echo "Error: D-Bus call failed: $raw" >&2
                return 1
            }
            # busctl plain output: 's "content..."' — extract and unescape C escapes.
            # Best-effort: busctl's plain format is not formally specified, so complex
            # embedded strings (e.g., literal backslash-n) may not round-trip perfectly.
            perl -e '$_=do{local $/;<STDIN>}; s/^s "//; s/"\s*$//; s/\\n/\n/g; s/\\t/\t/g; s/\\"/"/g; s/\\\\/\\/g; print' <<< "$raw"
        fi
    else
        echo "Error: No D-Bus CLI tool found (qdbus6, qdbus, or busctl required)" >&2
        exit 1
    fi
}

REPORT=$(call_dbus) || {
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
# Use printf to avoid heredoc delimiter collision if the report contains the delimiter.
printf '%s\n' "$REPORT" > "$STAGING/report.md"

# 2. Config file (redact home paths)
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/plasmazones"
# Guard against empty or root HOME (e.g., running from a systemd service without User=,
# or running as root where HOME=/ would mangle every absolute path).
if [[ -z "${HOME:-}" ]]; then
    echo "Warning: \$HOME is not set — skipping home path redaction" >&2
elif [[ "$HOME" = "/" ]]; then
    echo "Warning: \$HOME is / — skipping home path redaction to avoid mangling all paths" >&2
fi
redact_home() {
    if [[ -n "${HOME:-}" ]] && [[ "$HOME" != "/" ]]; then
        # Pass HOME via environment to avoid shell quoting issues (e.g., HOME containing
        # single quotes). Perl reads $ENV{HOME} directly, and \Q..\E quotes it as literal.
        # Handles both file arguments and piped stdin (perl reads stdin when no files given).
        HOME="$HOME" perl -pe 's/\Q$ENV{HOME}\E(?=[\/\s]|$)/~/g' -- "$@"
    elif [[ $# -gt 0 ]]; then
        cat "$@"
    else
        cat
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
    # Copy tree structure, redacting home paths in text files.
    # Use -print0/read -d '' for filenames with newlines or special chars.
    # Warn if files are skipped due to depth limit so triagers know the archive is incomplete.
    SKIPPED_DEEP=$(find -P "$DATA_DIR" -mindepth 6 -type f 2>/dev/null | head -1)
    if [[ -n "$SKIPPED_DEEP" ]]; then
        echo "Warning: some data files nested deeper than 5 levels were skipped" >&2
    fi
    find -P "$DATA_DIR" -maxdepth 5 -type f -print0 | while IFS= read -r -d '' f; do
        rel="${f#"$DATA_DIR"/}"
        mkdir -p "$STAGING/data/$(dirname "$rel")"
        case "$f" in
            *.json|*.js|*.glsl|*.frag|*.vert|*.conf|*.txt|*.qml|*.yaml|*.yml|*.toml|*.css|*.ini)
                redact_home "$f" > "$STAGING/data/$rel" ;;
            *)
                cp "$f" "$STAGING/data/$rel" ;;
        esac
    done
fi

# 5. Journal logs
# This duplicates the journal section in report.md but provides raw log lines
# (no Markdown wrapping) for easier grep/analysis by triagers.
if command -v journalctl &>/dev/null; then
    # Use the script's SINCE_MINUTES for local journal collection.
    # If 0 was passed (daemon default), fall back since journalctl needs a real value.
    JOURNAL_SINCE="${SINCE_MINUTES}"
    if [[ "$JOURNAL_SINCE" -eq 0 ]]; then
        JOURNAL_SINCE=30  # DefaultSinceMinutes
    fi

    # Use timeout if available, otherwise run journalctl directly (may hang on broken journal).
    if command -v timeout &>/dev/null; then
        _jctl() { timeout 15 journalctl "$@"; }
    else
        _jctl() { journalctl "$@"; }
    fi

    collect_journal() {
        local out err exit_code=0
        # Capture stdout and stderr separately so journalctl warnings
        # (e.g., "No entries") don't pollute the log output.
        err=$(mktemp "${TMPDIR:-/tmp}/pz-journal-err.XXXXXX")
        # Ensure temp file is cleaned up even if the script is killed mid-function.
        trap 'rm -f "$err"; trap - RETURN' RETURN
        out=$(_jctl --user "$@" \
            --since "$JOURNAL_SINCE min ago" \
            --no-pager -o short-iso 2>"$err") || exit_code=$?
        if [[ $exit_code -ne 0 ]] && [[ $exit_code -ne 1 ]]; then
            # exit 1 = no entries matched; 124 = timeout killed the process;
            # anything else is an actual error
            echo "Warning: journalctl failed (exit $exit_code): $(cat "$err")" >&2
        fi
        rm -f "$err"
        printf '%s' "$out"
    }

    JOURNAL=$(collect_journal -t plasmazonesd)

    # Fallback: try --identifier if -t returned nothing
    if [[ -z "${JOURNAL:-}" ]]; then
        JOURNAL=$(collect_journal --identifier=plasmazonesd)
    fi

    # Truncate to MaxLogLines, then redact via temp file to avoid SIGPIPE
    if [[ -n "${JOURNAL:-}" ]]; then
        printf '%s\n' "$JOURNAL" > "$STAGING/journal.raw"
        head -n "$MAX_LOG_LINES" "$STAGING/journal.raw" | redact_home > "$STAGING/journal.log"
        rm -f "$STAGING/journal.raw"
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
