#!/bin/bash
# Generate packaging changelogs from CHANGELOG.md
#
# Usage:
#   ./generate-changelog.sh debian   [version]  - Generate packaging/debian/changelog
#   ./generate-changelog.sh rpm      [version]  - Update %changelog in RPM spec
#   ./generate-changelog.sh notes    <version>  - Print GitHub release notes to stdout
#   ./generate-changelog.sh all      [version]  - Generate both debian and rpm
#
# If version is omitted for debian/rpm/all, all versions are included.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CHANGELOG="$PROJECT_DIR/CHANGELOG.md"
MAINTAINER="fuddlesworth <fuddlesworth@users.noreply.github.com>"

if [[ ! -f "$CHANGELOG" ]]; then
    echo "Error: CHANGELOG.md not found at $CHANGELOG" >&2
    exit 1
fi

# Parse CHANGELOG.md into structured data.
# Outputs lines: VERSION|DATE|BULLET for each entry.
parse_changelog() {
    awk '
    /^## \[/ {
        # Extract version and date: ## [1.5.2] - 2026-02-05
        # POSIX awk compatible: use index/substr instead of gawk match()
        i = index($0, "[")
        j = index($0, "]")
        version = substr($0, i+1, j-i-1)
        k = index($0, "] - ")
        date = substr($0, k+4)
        next
    }
    /^### / { next }  # Skip section headers (Added, Fixed, etc.)
    /^- / && version {
        # Strip leading "- " and print
        line = substr($0, 3)
        printf "%s|%s|%s\n", version, date, line
    }
    /^  / && version {
        # Continuation line (indented under a bullet)
        gsub(/^  /, "", $0)
        printf "%s|%s|  %s\n", version, date, $0
    }
    /^$/ { next }
    /^[^#\-\[ ]/ && version {
        # Paragraph text (like "Initial packaged release...")
        printf "%s|%s|%s\n", version, date, $0
    }
    ' "$CHANGELOG"
}

# Convert ISO date (2026-02-05) to RFC 2822 (Wed, 05 Feb 2026 00:00:00 +0000)
iso_to_rfc2822() {
    date -d "$1" -u '+%a, %d %b %Y %H:%M:%S +0000' 2>/dev/null || \
    python3 -c "
from datetime import datetime
d = datetime.strptime('$1', '%Y-%m-%d')
print(d.strftime('%a, %d %b %Y 00:00:00 +0000'))
" 2>/dev/null || \
    echo "$1"
}

# Convert ISO date (2026-02-05) to RPM format (Wed Feb  5 2026)
iso_to_rpm() {
    date -d "$1" -u '+%a %b %e %Y' 2>/dev/null || \
    python3 -c "
from datetime import datetime
d = datetime.strptime('$1', '%Y-%m-%d')
print(d.strftime('%a %b ') + str(d.day).rjust(2) + d.strftime(' %Y'))
" 2>/dev/null || \
    echo "$1"
}

# Generate Debian changelog
generate_debian() {
    local filter_version="${1:-}"
    local outfile="$SCRIPT_DIR/debian/changelog"
    local current_version="" current_date="" first_entry=1
    local tmpfile
    tmpfile=$(mktemp)

    while IFS='|' read -r version date bullet; do
        if [[ -n "$filter_version" && "$version" != "$filter_version" ]]; then
            continue
        fi

        if [[ "$version" != "$current_version" ]]; then
            # Close previous entry
            if [[ -n "$current_version" ]]; then
                echo ""
                echo " -- $MAINTAINER  $(iso_to_rfc2822 "$current_date")"
                echo ""
            fi

            current_version="$version"
            current_date="$date"
            echo "plasmazones ($version-1) unstable; urgency=medium"
            echo ""
        fi

        # Continuation lines start with spaces
        if [[ "$bullet" == "  "* ]]; then
            echo "   $bullet"
        else
            echo "  * $bullet"
        fi
    done < <(parse_changelog) > "$outfile"

    # Close final entry (append the trailing -- line)
    if [[ -n "$current_version" ]]; then
        {
            echo ""
            echo " -- $MAINTAINER  $(iso_to_rfc2822 "$current_date")"
            echo ""
        } >> "$outfile"
    fi

    echo "Generated $outfile" >&2
}

# Generate RPM %changelog section and splice into spec
generate_rpm() {
    local filter_version="${1:-}"
    local specfile="$SCRIPT_DIR/rpm/plasmazones.spec"
    local current_version="" current_date=""
    local tmpfile
    tmpfile=$(mktemp)

    while IFS='|' read -r version date bullet; do
        if [[ -n "$filter_version" && "$version" != "$filter_version" ]]; then
            continue
        fi

        if [[ "$version" != "$current_version" ]]; then
            # Add blank line between entries (not before first)
            if [[ -n "$current_version" ]]; then
                echo ""
            fi

            current_version="$version"
            current_date="$date"
            echo "* $(iso_to_rpm "$date") fuddlesworth - $version-1"
        fi

        if [[ "$bullet" == "  "* ]]; then
            echo " $bullet"
        else
            echo "- $bullet"
        fi
    done < <(parse_changelog) > "$tmpfile"

    # Replace everything after %changelog in the spec
    if [[ -f "$specfile" ]]; then
        local headfile
        headfile=$(mktemp)
        sed '/%changelog/q' "$specfile" > "$headfile"
        cat "$headfile" "$tmpfile" > "$specfile"
        rm -f "$headfile"
        echo "Updated %changelog in $specfile" >&2
    else
        echo "Warning: $specfile not found, printing to stdout" >&2
        cat "$tmpfile"
    fi

    rm -f "$tmpfile"
}

# Extract release notes for a specific version (markdown)
generate_notes() {
    local version="$1"
    if [[ -z "$version" ]]; then
        echo "Error: version required for notes output" >&2
        exit 1
    fi

    # Extract the raw markdown section for this version from CHANGELOG.md
    # Stops at the next version header or the link reference block
    awk -v ver="$version" '
    /^## \[/ {
        if (found) exit
        if (index($0, "[" ver "]")) { found=1; next }
    }
    found && /^\[/ { exit }
    found { print }
    ' "$CHANGELOG" | sed -e :a -e '/^[[:space:]]*$/{ $d; N; ba; }'
}

case "${1:-}" in
    debian)
        generate_debian "${2:-}"
        ;;
    rpm)
        generate_rpm "${2:-}"
        ;;
    notes)
        generate_notes "${2:-}"
        ;;
    all)
        generate_debian "${2:-}"
        generate_rpm "${2:-}"
        ;;
    *)
        echo "Usage: $0 {debian|rpm|notes|all} [version]" >&2
        exit 1
        ;;
esac
