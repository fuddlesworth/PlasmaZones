#!/usr/bin/bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate a PlasmaZones support report archive.
#
# Collects the daemon's redacted Markdown report, config files, layout files,
# and journal logs into a timestamped .tar.gz archive for attaching to
# GitHub Issues or Discussions.
#
# The D-Bus report (report.md) already contains config/session/rules/layout
# summaries, but we also include the raw files (the whole config dir plus
# data/) so that triagers can inspect exact JSON without re-serialization
# artefacts.
#
# Requires: plasmazonesd running, busctl (or qdbus6/qdbus), perl (with JSON::PP for busctl)

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
            echo "  rules.json       Window/screen rules (home paths redacted)"
            echo "  ...              Remaining config-dir files (quick layouts, settings profiles, etc.)"
            echo "  data/            User data (layouts, algorithms, shaders, animation profiles, etc.)"
            echo "  journal.log      Recent plasmazonesd journal entries"
            echo "  kwin-effect.log  Recent PlasmaZones KWin effect journal entries"
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
    # Prefer busctl: qttools' `qdbus`/`qdbus6` segfaults at process exit on
    # Qt 6.11+ (static-destruction-order crash in registerComplexDBusType ->
    # QMetaType::unregisterMetaType) whenever it introspects an object that
    # exposes complex D-Bus types — which /PlasmaZones does. That crash can
    # discard buffered stdout, losing the report entirely. busctl (systemd)
    # is unaffected and present on every systemd distro, so it is the default.
    if command -v busctl &>/dev/null; then
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
    elif command -v qdbus6 &>/dev/null; then
        # Fallback only — see the busctl rationale above re: the Qt 6.11+ crash.
        qdbus6 org.plasmazones /PlasmaZones org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    elif command -v qdbus &>/dev/null; then
        qdbus org.plasmazones /PlasmaZones org.plasmazones.Control.generateSupportReport "$SINCE_MINUTES"
    else
        echo "Error: No D-Bus CLI tool found (busctl, qdbus6, or qdbus required)" >&2
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

# 2. Config directory (redact home paths in text files)
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

# Copy the entire config tree, not just config.json/session.json: rules.json,
# quicklayouts.json, layout-settings.json, and the settings-profiles store
# (profiles/) all shape effective behaviour, and their absence made several
# reports untriageable (discussions #795/#796). Files land at the archive root
# mirroring the on-disk layout, so config.json/session.json keep their
# established locations. Window classes/titles are kept for diagnostic value;
# home paths are redacted.
if [[ -d "$CONFIG_DIR" ]]; then
    # `|| true` absorbs SIGPIPE-141 when head closes the pipe while find is
    # still writing — under `set -euo pipefail` that would otherwise abort
    # the whole script precisely when deep nesting exists.
    SKIPPED_CONF_DEEP=$(find -P "$CONFIG_DIR" -mindepth 4 -type f 2>/dev/null | head -1 || true)
    if [[ -n "$SKIPPED_CONF_DEEP" ]]; then
        echo "Warning: some config files nested deeper than 3 levels were skipped" >&2
    fi
    find -P "$CONFIG_DIR" -maxdepth 3 -type f -print0 | while IFS= read -r -d '' f; do
        rel="${f#"$CONFIG_DIR"/}"
        # Reserved names are generated by this script (report.md above, the
        # journal logs below) or claimed by the DATA_DIR tree; a config-dir
        # entry with the same name must not fight them for the slot.
        case "$rel" in
            report.md|journal.log|kwin-effect.log|data|data/*)
                echo "Warning: skipping config entry '$rel' (name reserved by the archive layout)" >&2
                continue ;;
        esac
        mkdir -p "$STAGING/$(dirname "$rel")"
        # Redact every text file, not just known extensions: config-dir files
        # are the likeliest to embed home paths, and an extensionless text
        # config copied verbatim would leak them into a report meant for
        # public attachment. grep -I detects binary content.
        if grep -Iq . "$f" 2>/dev/null; then
            redact_home "$f" > "$STAGING/$rel"
        else
            cp "$f" "$STAGING/$rel"
        fi
    done
fi

# 3. User data directory (layouts, custom algorithms, shaders, etc.)
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/plasmazones"
if [[ -d "$DATA_DIR" ]]; then
    mkdir -p "$STAGING/data"
    # Copy tree structure, redacting home paths in text files.
    # Use -print0/read -d '' for filenames with newlines or special chars.
    # Warn if files are skipped due to depth limit so triagers know the archive is incomplete.
    # Same SIGPIPE absorption as the config-dir guard above.
    SKIPPED_DEEP=$(find -P "$DATA_DIR" -mindepth 6 -type f 2>/dev/null | head -1 || true)
    if [[ -n "$SKIPPED_DEEP" ]]; then
        echo "Warning: some data files nested deeper than 5 levels were skipped" >&2
    fi
    find -P "$DATA_DIR" -maxdepth 5 -type f -print0 | while IFS= read -r -d '' f; do
        rel="${f#"$DATA_DIR"/}"
        mkdir -p "$STAGING/data/$(dirname "$rel")"
        # Text detection instead of an extension allowlist, matching the
        # config-dir loop: an allowlist misses real text formats (a
        # user-authored .luau algorithm, for one) and would ship them with
        # home paths intact.
        if grep -Iq . "$f" 2>/dev/null; then
            redact_home "$f" > "$STAGING/data/$rel"
        else
            cp "$f" "$STAGING/data/$rel"
        fi
    done
fi

# 4. Journal logs
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

    # collect_journal <scope> <journalctl-args...>
    #   <scope> is "--user" or "--system".
    collect_journal() {
        local scope="$1"; shift
        local out err exit_code=0
        # Capture stdout and stderr separately so journalctl warnings
        # (e.g., "No entries") don't pollute the log output.
        err=$(mktemp "${TMPDIR:-/tmp}/pz-journal-err.XXXXXX")
        # Ensure temp file is cleaned up even if the script is killed mid-function.
        trap 'rm -f "$err"; trap - RETURN' RETURN
        out=$(_jctl "$scope" "$@" \
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

    JOURNAL=$(collect_journal --user -t plasmazonesd)

    # Fallback: try --identifier if -t returned nothing
    if [[ -z "${JOURNAL:-}" ]]; then
        JOURNAL=$(collect_journal --user --identifier=plasmazonesd)
    fi

    # Truncate to the most recent MaxLogLines (the entries around a failure),
    # then redact via temp file to avoid SIGPIPE. tail keeps the newest lines,
    # matching SupportReport::capLogLines() in src/core/supportreport.cpp.
    if [[ -n "${JOURNAL:-}" ]]; then
        printf '%s\n' "$JOURNAL" > "$STAGING/journal.raw"
        tail -n "$MAX_LOG_LINES" "$STAGING/journal.raw" | redact_home > "$STAGING/journal.log"
        rm -f "$STAGING/journal.raw"
    fi

    # KWin effect logs: the effect runs inside the kwin_wayland process, so its
    # journal is tagged "kwin_wayland", not "plasmazonesd". Filter to PlasmaZones
    # lines (every effect log category contains "plasmazones"). Without this a
    # non-loading effect — the most common "drags/shortcuts do nothing" cause —
    # leaves no trace in the archive.
    KWIN_JOURNAL=$(collect_journal --user -t kwin_wayland)
    if [[ -z "${KWIN_JOURNAL:-}" ]]; then
        KWIN_JOURNAL=$(collect_journal --user --identifier=kwin_wayland)
    fi
    # Fall back to the system journal: a compositor that is not a systemd user
    # service logs there instead of the user journal.
    if [[ -z "${KWIN_JOURNAL:-}" ]]; then
        KWIN_JOURNAL=$(collect_journal --system -t kwin_wayland)
    fi
    if [[ -n "${KWIN_JOURNAL:-}" ]]; then
        printf '%s\n' "$KWIN_JOURNAL" > "$STAGING/kwin-effect.raw"
        grep -i plasmazones "$STAGING/kwin-effect.raw" 2>/dev/null \
            | tail -n "$MAX_LOG_LINES" | redact_home > "$STAGING/kwin-effect.log" || true
        rm -f "$STAGING/kwin-effect.raw"
        # grep matched nothing → drop the empty file rather than ship a blank.
        [[ -s "$STAGING/kwin-effect.log" ]] || rm -f "$STAGING/kwin-effect.log"
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
