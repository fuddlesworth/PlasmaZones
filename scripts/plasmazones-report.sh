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
# Requires: plasmazonesd running, busctl (or qdbus6/qdbus), python3 (stdlib
# only). python3 replaced perl here: the old busctl path needed JSON::PP,
# which is a Perl core module upstream but ships as a separate, not always
# installed package on several distros ("Can't locate JSON/PP.pm in @INC").

set -euo pipefail

# These defaults mirror src/core/platform/supportreport.cpp — keep in sync.
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
            echo "  kwin-effect.log  Recent kwin_wayland journal entries (the effect runs inside it)"
            echo "  kglobalaccel.txt Effective KGlobalAccel bindings for the plasmazonesd component"
            echo "  kwin-effects.txt Enabled/loaded KWin desktop effects (kwinrc [Plugins] + live D-Bus state)"
            echo "  kwin-rules.txt   KWin's own window rules (kwinrulesrc, window titles redacted)"
            echo ""
            echo "The archive is meant to be attached to a bug report. Home paths are redacted,"
            echo "but it still records your machine hostname in the journal lines, the class and"
            echo "title of tracked windows, the match patterns from your window rules, and the"
            echo "manufacturer, model and serial number your monitors report over EDID."
            echo "Look it over before you post it."
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Bind HOME once, before anything expands it. Under `set -u` an unset HOME is
# a fatal error at the first bare $HOME, which is the CONFIG_DIR default below
# — three lines above the guard written to warn about exactly that case, so the
# warning could never fire. Binding it to a defined empty string keeps the
# guard reachable: every HOME-derived path then fails its own -d or -f test and
# is skipped, and redact_home takes its passthrough arm.
HOME="${HOME:-}"

# Resolve output directory to an absolute path so the final message
# remains useful regardless of later CWD changes.
if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="${TMPDIR:-/tmp}"
fi
mkdir -p -- "$OUTPUT_DIR"
OUTPUT_DIR=$(cd -- "$OUTPUT_DIR" && pwd)

# python3 is required for JSON parsing and home-path redaction. Check up
# front with a clear error: a missing interpreter failing mid-pipeline
# would surface as the misleading "Could not connect to PlasmaZones
# daemon." message (the exact failure mode the JSON::PP removal fixed).
if ! command -v python3 &>/dev/null; then
    echo "Error: python3 is required but was not found in PATH." >&2
    exit 1
fi

# ─── D-Bus call ───────────────────────────────────────────────────────────────

call_dbus() {
    # Prefer busctl: qttools' `qdbus`/`qdbus6` segfaults at process exit on
    # Qt 6.11+ (static-destruction-order crash in registerComplexDBusType ->
    # QMetaType::unregisterMetaType) whenever it introspects an object that
    # exposes complex D-Bus types — which /PlasmaZones does. That crash can
    # discard buffered stdout, losing the report entirely. busctl (systemd)
    # is unaffected and present on every systemd distro, so it is the default.
    # 90s, well above busctl's own 25s default. The daemon collects the journal
    # inside this call (three journalctl attempts per section, each bounded at
    # 12s), so a slow journal can legitimately take over a minute, and the
    # default would abandon a healthy daemon and report it as not running.
    local bus_timeout=(--timeout=90)
    if command -v busctl &>/dev/null; then
        local raw
        if raw=$(busctl --user "${bus_timeout[@]}" --json=short call org.plasmazones /PlasmaZones org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>/dev/null); then
            # Parse busctl JSON output: {"type":"s","data":["..."]}
            python3 -c 'import json, sys; sys.stdout.write(json.load(sys.stdin)["data"][0])' <<< "$raw"
        else
            raw=$(busctl --user "${bus_timeout[@]}" call org.plasmazones /PlasmaZones org.plasmazones.Control generateSupportReport i "$SINCE_MINUTES" 2>&1) || {
                echo "Error: D-Bus call failed: $raw" >&2
                return 1
            }
            # busctl plain output: 's "content..."' — extract and unescape C escapes.
            # Best-effort: busctl's plain format is not formally specified, so complex
            # embedded strings (e.g., literal backslash-n) may not round-trip perfectly.
            python3 -c '
import re, sys
s = sys.stdin.read()
s = re.sub(r"^s \"", "", s)
s = re.sub(r"\"\s*$", "", s)
s = s.replace("\\n", "\n").replace("\\t", "\t").replace("\\\"", "\"").replace("\\\\", "\\")
sys.stdout.write(s)' <<< "$raw"
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
# The staging dir holds journal text, so a Ctrl-C partway through collection
# must not leave it behind. Cleanup lives on EXIT alone, which is the only
# terminal arm: a bash signal trap runs its handler and then RESUMES at the
# interrupted point, so putting the rm on INT/TERM deleted the directory and
# then carried on writing into it. The signal arms instead exit with the
# conventional 128+signo, which fires the EXIT trap and cleans up on the way
# out.
trap 'rm -rf "$STAGING"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# 1. Markdown report from daemon (already redacted)
# Use printf to avoid heredoc delimiter collision if the report contains the delimiter.
printf '%s\n' "$REPORT" > "$STAGING/report.md"

# The daemon cannot know what this script packs around its report, so the
# archive listing is appended here (outside the <details> block so it stays
# visible when the report is pasted collapsed).
cat >> "$STAGING/report.md" <<'EOF'

## Archive Contents
This report ships in an archive with the raw files behind the summaries above:
- `config.json`, `session.json` (full window session state), `rules.json` and the remaining config-dir files (quick layouts, layout settings, settings profiles)
- `data/` with user layouts, algorithms, shaders and animation profiles
- `journal.log` and `kwin-effect.log` with the raw journal lines
- `kglobalaccel.txt` with the effective shortcut bindings and `kwin-effects.txt` with the enabled/loaded KWin effects
- `kwin-rules.txt` with KWin's own window rules, which move and re-desktop windows independently of PlasmaZones (window titles, rule descriptions and client machine names redacted)

Text files have home and XDG paths replaced with `~`, `$XDG_CONFIG_HOME` and `$XDG_DATA_HOME`. Non-text files such as shader pack previews are left out, because their embedded metadata can carry paths that redaction cannot reach. When anything is omitted that way, its name is listed in `binary-files-omitted.txt`. If that file is not in this archive, nothing was omitted.
EOF

# 2. Config directory (redact home paths in text files)
# Strip a trailing slash before anything builds a pattern from HOME. The
# redaction lookahead below requires a separator AFTER the match, so a HOME of
# "/home/u/" would demand two and never match "/home/u/.config", silently
# disabling redaction everywhere. The C++ side is immune because
# QDir::homePath() cleans the path for it.
while [[ ${#HOME} -gt 1 && "$HOME" == */ ]]; do
    HOME="${HOME%/}"
done

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
        # Pass the paths via environment to avoid shell quoting issues (e.g. a
        # HOME containing quotes); re.escape quotes each as a literal.
        # Byte-oriented (environb, "rb", buffer) so a non-UTF-8 path or stray
        # bytes in a journal line pass through unmangled. Processes line by
        # line like the old perl -pe, so large inputs are never held fully in
        # memory; $ in the lookahead matches at each (chomped or final) line
        # end. Handles both file arguments and piped stdin (no files given).
        #
        # XDG_CONFIG_HOME and XDG_DATA_HOME are redacted alongside HOME, and
        # LONGEST FIRST so a path nested under another is replaced by the more
        # specific variable rather than half-rewritten by the shorter one.
        # This mirrors SupportReport::buildRedactions in
        # src/core/platform/supportreport.cpp, which the header of this script
        # says to keep in sync: with either XDG var pointed outside home, the
        # daemon's own report.md was redacted while every file staged here
        # still carried the absolute path, username included.
        HOME="$HOME" \
            PZ_XDG_CONFIG="${XDG_CONFIG_HOME:-}" \
            PZ_XDG_DATA="${XDG_DATA_HOME:-}" \
            python3 -c '
import os, re, sys
subs = []
for var, repl in ((b"PZ_XDG_CONFIG", b"$XDG_CONFIG_HOME"),
                  (b"PZ_XDG_DATA", b"$XDG_DATA_HOME"),
                  (b"HOME", b"~")):
    val = os.environb.get(var, b"")
    if val and val != b"/":
        subs.append((val.rstrip(b"/"), repl))
# Longest literal first: with XDG_CONFIG_HOME under HOME, replacing HOME first
# would leave "~/.config/plasmazones" instead of "$XDG_CONFIG_HOME".
subs.sort(key=lambda s: len(s[0]), reverse=True)
pats = [(re.compile(re.escape(v) + rb"(?=[/\s]|$)", re.M), r) for v, r in subs]
out = sys.stdout.buffer
def redact(stream):
    for line in stream:
        for pat, repl in pats:
            line = pat.sub(repl, line)
        out.write(line)
files = sys.argv[1:]
if files:
    for f in files:
        with open(f, "rb") as fh:
            redact(fh)
else:
    redact(sys.stdin.buffer)' "$@"
    elif [[ $# -gt 0 ]]; then
        cat "$@"
    else
        cat
    fi
}

# Stage a whole tree in ONE python process, reading NUL-separated src/dst pairs
# on stdin. The per-file alternative spawned an interpreter per file, and a data
# dir with shader packs, curves and animation profiles is easily 100+ files —
# ~100 interpreter startups on a path the user runs while a bug is live.
#
# Failure is isolated PER FILE, which the surrounding `set -e` plus `pipefail`
# would not give a shell loop: one unreadable file, or a full disk, warns and
# the rest of the archive still ships. Losing one config file is worth far less
# than losing the archive, the same trade the journal sections make.
#
# Non-text files are listed, never copied — see the config-loop comment for why
# (embedded absolute paths in asset metadata that redaction cannot reach, and
# UTF-16 text that grep -I calls binary).
redact_pairs() {
    HOME="${HOME:-}" \
        PZ_XDG_CONFIG="${XDG_CONFIG_HOME:-}" \
        PZ_XDG_DATA="${XDG_DATA_HOME:-}" \
        PZ_BINLIST="$1" \
        PZ_STAGING="$STAGING" \
        python3 -c '
import os, re, sys
subs = []
for var, repl in ((b"PZ_XDG_CONFIG", b"$XDG_CONFIG_HOME"),
                  (b"PZ_XDG_DATA", b"$XDG_DATA_HOME"),
                  (b"HOME", b"~")):
    val = os.environb.get(var, b"")
    if val and val != b"/":
        subs.append((val.rstrip(b"/"), repl))
subs.sort(key=lambda s: len(s[0]), reverse=True)
pats = [(re.compile(re.escape(v) + rb"(?=[/\s]|$)", re.M), r) for v, r in subs]
binlist = os.environb.get(b"PZ_BINLIST", b"")
staging = os.environb.get(b"PZ_STAGING", b"").rstrip(b"/")

def is_text(path):
    # Same call grep -I makes: a NUL byte in the first block means binary.
    with open(path, "rb") as fh:
        return b"\0" not in fh.read(8192)

raw = sys.stdin.buffer.read().split(b"\0")
# Trailing empty element from the final separator.
pairs = [(raw[i], raw[i + 1]) for i in range(0, len(raw) - 1, 2) if raw[i]]
for src, dst in pairs:
    try:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if is_text(src):
            with open(src, "rb") as fh, open(dst, "wb") as out:
                for line in fh:
                    for pat, repl in pats:
                        line = pat.sub(repl, line)
                    out.write(line)
        elif binlist:
            # Record the ARCHIVE-relative path, so the listing reads the way
            # the tarball does rather than exposing the staging directory.
            rel = dst
            if staging and rel.startswith(staging + b"/"):
                rel = rel[len(staging) + 1:]
            with open(binlist, "ab") as bl:
                bl.write(rel + b"\n")
    except OSError as exc:
        sys.stderr.write("Warning: skipping %s (%s)\n"
                         % (src.decode("utf-8", "replace"), exc))
        try:
            os.unlink(dst)
        except OSError:
            pass
' "$@"
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
            report.md|journal.log|journal.raw|kwin-effect.log|kwin-effect.raw|kglobalaccel.txt|kglobalaccel.raw|kwin-effects.txt|kwin-effects.raw|kwin-rules.txt|kwin-rules.raw|binary-files-omitted.txt|data|data/*)
                echo "Warning: skipping config entry '$rel' (name reserved by the archive layout)" >&2
                continue ;;
        esac
        # Emit a src/dst pair; redact_pairs below does the staging in one
        # process, with per-file failure isolation and the binary policy.
        #
        # Non-text files are NOT copied. A NUL byte in the first block means
        # binary, which covers genuine assets (a shader pack's preview.png)
        # whose container metadata routinely embeds the authoring tool's
        # absolute source path — bytes no line-oriented redaction can reach.
        # It also covers UTF-16/BOM'd text, which would otherwise ship with
        # home paths intact, the exact case this text detection exists to
        # prevent. Neither belongs in an archive meant for public attachment
        # and neither diagnoses a placement bug, so they are listed by name.
        printf '%s\0%s\0' "$f" "$STAGING/$rel"
    done | redact_pairs "$STAGING/binary-files-omitted.txt"
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
        # Text detection instead of an extension allowlist, matching the
        # config-dir loop: an allowlist misses real text formats (a
        # user-authored .luau algorithm, for one) and would ship them with
        # home paths intact. Same batching, same per-file isolation and same
        # binary policy as that loop — see the comment there.
        printf '%s\0%s\0' "$f" "$STAGING/data/$rel"
    done | redact_pairs "$STAGING/binary-files-omitted.txt"
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
        # Inside $STAGING so the EXIT trap covers it too: a RETURN trap does not
        # fire on a signal, and this function is called up to five times.
        err=$(mktemp "$STAGING/pz-journal-err.XXXXXX")
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
    # matching capLogLines() in src/core/platform/supportreport.cpp.
    #
    # Guarded as a unit: under `set -e` a write failure here (a full disk, a
    # redactor that will not start) would abort the whole run after the report
    # and the config tree are already staged, and the EXIT trap would then
    # delete all of it. One missing diagnostics file is worth far less than the
    # archive, so the section warns and the run continues. The redacted file is
    # dropped on failure rather than shipped half-written.
    if [[ -n "${JOURNAL:-}" ]]; then
        if ! { printf '%s\n' "$JOURNAL" > "$STAGING/journal.raw" \
            && tail -n "$MAX_LOG_LINES" "$STAGING/journal.raw" | redact_home > "$STAGING/journal.log"; }; then
            echo "Warning: could not collect the plasmazonesd journal" >&2
            rm -f "$STAGING/journal.log"
        fi
        rm -f "$STAGING/journal.raw"
    fi

    # Compositor logs: the effect runs inside the kwin_wayland process, so its
    # journal is tagged "kwin_wayland", not "plasmazonesd". Without this a
    # non-loading effect — the most common "drags/shortcuts do nothing" cause —
    # leaves no trace in the archive.
    #
    # The whole window is kept. This used to grep for "plasmazones" on the
    # premise that every effect log category contains it, but KWin installs its
    # own message handler and does not print the category, so effect lines
    # arrive bare and only the ones quoting a PlasmaZones window id matched.
    # Measured on a live session, that dropped 77% of them, and an archive with
    # no kwin-effect.log at all was read as "the effect logged nothing". The
    # tail below already bounds the file. See the matching note in
    # src/core/platform/supportreport.cpp.
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
        # Staged through a file for the plasmazonesd path's stated reason: with
        # printf as the pipeline's producer, tail closing early kills it with
        # SIGPIPE and pipefail turns that into a failure the old `|| true` had
        # to swallow — which also swallowed a genuine redact_home failure and
        # shipped a partially redacted file. Guarded as a unit for the same
        # reason as the sibling above: a failure warns rather than taking the
        # whole archive down with it.
        if ! { printf '%s\n' "$KWIN_JOURNAL" > "$STAGING/kwin-effect.raw" \
            && tail -n "$MAX_LOG_LINES" "$STAGING/kwin-effect.raw" | redact_home > "$STAGING/kwin-effect.log"; }; then
            echo "Warning: could not collect the kwin_wayland journal" >&2
            rm -f "$STAGING/kwin-effect.log"
        fi
        rm -f "$STAGING/kwin-effect.raw"
        # Nothing in the window → drop the empty file rather than ship a blank.
        [[ -s "$STAGING/kwin-effect.log" ]] || rm -f "$STAGING/kwin-effect.log"
    fi
fi

# 5. Global shortcut state (KGlobalAccel)
# The daemon registers shortcuts through KGlobalAccel with autoloading, so
# what actually gets grabbed is the binding stored in kglobalshortcutsrc —
# NOT the value in config.json, and the two can diverge (conflict prompts,
# System Settings edits). Without this capture, "shortcut is set but nothing
# happens" reports are untriageable from the archive alone (discussion #809).
# Only the plasmazonesd component is included; the full file lists every
# app's shortcuts, which the reporter did not agree to share.
{
    KGLOBAL_RC="${XDG_CONFIG_HOME:-$HOME/.config}/kglobalshortcutsrc"
    if [[ -f "$KGLOBAL_RC" ]]; then
        echo "── kglobalshortcutsrc [plasmazonesd] ──"
        awk '/^\[/{keep=($0=="[plasmazonesd]")} keep' "$KGLOBAL_RC"
        echo ""
    fi
    # Live registration state, best-effort: shows what KGlobalAccel is
    # actually grabbing right now, which catches divergence between the
    # on-disk rc and the running session.
    if command -v busctl &>/dev/null; then
        echo "── org.kde.kglobalaccel allShortcutInfos ──"
        busctl --user call org.kde.kglobalaccel /component/plasmazonesd \
            org.kde.kglobalaccel.Component allShortcutInfos 2>&1 || true
    fi
} > "$STAGING/kglobalaccel.raw" || true
# Redact as its own guarded step, never with `|| true` on the pipeline: that
# swallowed a genuine redact_home failure, and the -s check below only drops an
# EMPTY file, so a python3 that died mid-stream shipped a PARTIALLY REDACTED
# file in an archive whose whole purpose is public attachment. Same
# warn-and-delete shape the journal sections use.
if ! redact_home "$STAGING/kglobalaccel.raw" > "$STAGING/kglobalaccel.txt"; then
    echo "Warning: could not redact the kglobalaccel capture — dropping it" >&2
    rm -f "$STAGING/kglobalaccel.txt"
fi
rm -f "$STAGING/kglobalaccel.raw"
# Nothing captured (no rc section, no busctl) → drop the blank file.
[[ -s "$STAGING/kglobalaccel.txt" ]] || rm -f "$STAGING/kglobalaccel.txt"

# 6. KWin desktop effects
# Other effects interact with PlasmaZones (blur/contrast behind overlays,
# wobbly windows and translate-style effects fighting placement animations,
# a second tiling effect grabbing the same windows), so triagers need to know
# what is enabled. Two views, same rationale as the kglobalaccel capture:
# kwinrc [Plugins] is the persisted enable/disable state, while the D-Bus
# properties show what the running compositor actually loaded and what is
# animating right now — the two can diverge (unsupported effects, crashes,
# scripted toggles).
{
    KWIN_RC="${XDG_CONFIG_HOME:-$HOME/.config}/kwinrc"
    if [[ -f "$KWIN_RC" ]]; then
        echo "── kwinrc [Plugins] ──"
        awk '/^\[/{keep=($0=="[Plugins]")} keep' "$KWIN_RC"
        echo ""
    fi
    if command -v busctl &>/dev/null; then
        echo "── org.kde.KWin /Effects loadedEffects ──"
        busctl --user get-property org.kde.KWin /Effects \
            org.kde.kwin.Effects loadedEffects 2>&1 || true
        echo ""
        echo "── org.kde.KWin /Effects activeEffects ──"
        busctl --user get-property org.kde.KWin /Effects \
            org.kde.kwin.Effects activeEffects 2>&1 || true
    fi
} > "$STAGING/kwin-effects.raw" || true
# Guarded redaction step (see the kglobalaccel section for why `|| true` on the
# pipeline is unsafe here).
if ! redact_home "$STAGING/kwin-effects.raw" > "$STAGING/kwin-effects.txt"; then
    echo "Warning: could not redact the KWin effects capture — dropping it" >&2
    rm -f "$STAGING/kwin-effects.txt"
fi
rm -f "$STAGING/kwin-effects.raw"
# Nothing captured (no kwinrc, no busctl) → drop the blank file.
[[ -s "$STAGING/kwin-effects.txt" ]] || rm -f "$STAGING/kwin-effects.txt"

# 7. KWin window rules
# KWin's own rules move, resize and re-desktop windows underneath PlasmaZones,
# so a placement complaint can have nothing to do with PlasmaZones at all. The
# desktop fields are the ones that repay the capture: Rules::applyDesktops
# clears the membership list and repopulates it only from desktop IDs that still
# resolve, so a rule holding stale desktop UUIDs silently makes its windows
# on-all-desktops — and an empty list is exactly what isOnAllDesktops() means.
# A rule carrying position/size alongside that relocates the window in the same
# stroke, which reads as PlasmaZones throwing windows around.
#
# The free-text VALUES are dropped. This archive is meant to be attachable to a
# public issue (the whole reason home paths are redacted), and a window title
# carries document names, URLs and contact names, while the fields that diagnose
# a placement rule — wmclass, the desktop/position/size values and their *rule
# policy numbers — carry none. titlematch/titlerule are kept so a triager can
# still see that a title match exists and how it is applied.
#
# description= is dropped for the same reason as title=: it is the user's own
# name for the rule, and in practice it quotes the document, site or contact the
# rule was made for. clientmachine= is a bare hostname. Both carry exactly the
# class of data the title rule exists to keep out, and neither diagnoses
# anything. The pattern stays anchored at ^ so titlematch=/titlerule= and the
# other *rule policy numbers survive untouched.
{
    KWIN_RULES_RC="${XDG_CONFIG_HOME:-$HOME/.config}/kwinrulesrc"
    if [[ -f "$KWIN_RULES_RC" ]]; then
        sed -E 's/^(title|Title|description|Description|clientmachine|ClientMachine)=.*/\1=<redacted>/' \
            "$KWIN_RULES_RC"
    fi
} > "$STAGING/kwin-rules.raw" || true
# Guarded redaction step (see the kglobalaccel section for why `|| true` on the
# pipeline is unsafe here).
if ! redact_home "$STAGING/kwin-rules.raw" > "$STAGING/kwin-rules.txt"; then
    echo "Warning: could not redact the KWin rules capture — dropping it" >&2
    rm -f "$STAGING/kwin-rules.txt"
fi
rm -f "$STAGING/kwin-rules.raw"
# No kwinrulesrc (the common case — the user has never made a KWin rule) → drop
# the blank file rather than shipping a puzzling empty one.
[[ -s "$STAGING/kwin-rules.txt" ]] || rm -f "$STAGING/kwin-rules.txt"

# ─── Create archive ──────────────────────────────────────────────────────────

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
ARCHIVE_NAME="plasmazones-report-${TIMESTAMP}.tar.gz"
ARCHIVE_PATH="$OUTPUT_DIR/$ARCHIVE_NAME"

# The archive carries window titles, rule patterns and the whole config, so it
# is created owner-only rather than at whatever the ambient umask allows.
#
# Built inside the 0700 staging dir and MOVED into place, rather than tarred
# straight to the destination. The default output dir is $TMPDIR, where this
# timestamped name is guessable, and `rm -f` followed by `tar` is two syscalls:
# another local user can recreate a symlink at the path in between, and tar has
# no O_NOFOLLOW/O_EXCL, so it would write through it. The umask does not help —
# it governs the mode of a file tar CREATES, not one it follows into. `mv`
# replaces a symlink rather than writing through it, and the file is never
# world-reachable before it lands.
# The build directory is its own mktemp -d (0700), NOT $STAGING — an archive
# written inside the tree being tarred would try to include itself — and not
# $STAGING/.. either, which is the world-writable $TMPDIR this guards against.
ARCHIVE_BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/plasmazones-archive.XXXXXXXXXX")
trap 'rm -rf "$STAGING" "$ARCHIVE_BUILD_DIR"' EXIT
ARCHIVE_TMP="$ARCHIVE_BUILD_DIR/$ARCHIVE_NAME"
(umask 077; tar -czf "$ARCHIVE_TMP" -C "$STAGING" .)
mv -f "$ARCHIVE_TMP" "$ARCHIVE_PATH"

echo "Support report archive created:"
echo "  $ARCHIVE_PATH"
echo ""
echo "Contents:"
# `|| true`: under pipefail an all-blank listing would make grep -v exit
# non-zero and abort AFTER the archive was already written, losing the
# attach instruction below for a report that succeeded.
tar -tzf "$ARCHIVE_PATH" | sed 's|^./||' | grep -v '^$' | sed 's/^/  /' || true
echo ""
echo "Attach this file to your GitHub Issue or Discussion."
