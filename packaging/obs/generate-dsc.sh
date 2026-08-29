#!/bin/bash
# Generate the OBS Debian recipe (plasmazones.dsc) from packaging/debian/control.
#
# Usage:
#   ./generate-dsc.sh <version> [revision] [outfile]
#
# OBS builds .deb targets from a .dsc recipe rather than from debian/control,
# and its dependency resolver reads Build-Depends out of that .dsc *before*
# debtransform assembles the real source package. So the .dsc must carry an
# accurate copy of the field. Deriving it here instead of hand-maintaining a
# second copy keeps packaging/debian/control the single source of truth.
#
# The .dsc deliberately has no Files:/Checksums-Sha256: section. OBS runs
# debtransform (obs-build), which discovers the tarball fetched by the
# download_files source service, folds in the debian.* files, and regenerates
# the source package with correct checksums. A hand-written checksum would be
# both unknowable at commit time and immediately wrong.
#
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONTROL="$SCRIPT_DIR/../debian/control"

VERSION="${1:-}"
REVISION="${2:-1}"
OUTFILE="${3:-$SCRIPT_DIR/plasmazones.dsc}"

if [[ -z "$VERSION" ]]; then
    echo "Usage: $0 <version> [revision] [outfile]" >&2
    exit 1
fi

if [[ ! -f "$CONTROL" ]]; then
    echo "Error: $CONTROL not found" >&2
    exit 1
fi

# Flatten the multi-line Build-Depends field into the single logical line a
# .dsc uses. Continuation lines in debian/control are the ones starting with
# whitespace; the field ends at the next field name or a blank line.
BUILD_DEPENDS="$(awk '
    /^Build-Depends:/ { collecting = 1; sub(/^Build-Depends:[[:space:]]*/, ""); if ($0 != "") print; next }
    collecting && /^[[:space:]]/ { sub(/^[[:space:]]+/, ""); sub(/[[:space:]]+$/, ""); if ($0 != "") print; next }
    collecting { collecting = 0 }
' "$CONTROL" | paste -sd' ' - | sed 's/,$//')"

if [[ -z "$BUILD_DEPENDS" ]]; then
    echo "Error: could not parse Build-Depends from $CONTROL" >&2
    exit 1
fi

# Sanity-check that the parse actually captured the whole field rather than a
# truncated prefix. kwin-dev is the last dependency that matters most (an
# unresolvable KWin is the failure mode that strands the effect plugin), so its
# absence means the awk above silently stopped early.
if [[ "$BUILD_DEPENDS" != *"kwin-dev"* ]]; then
    echo "Error: parsed Build-Depends is missing kwin-dev, parse likely truncated" >&2
    echo "Got: $BUILD_DEPENDS" >&2
    exit 1
fi

cat > "$OUTFILE" << EOF
Format: 3.0 (quilt)
Source: plasmazones
Binary: plasmazones
Architecture: any
Version: $VERSION-$REVISION
Maintainer: fuddlesworth <fuddlesworth@users.noreply.github.com>
Homepage: https://github.com/fuddlesworth/PlasmaZones
Standards-Version: 4.6.2
Build-Depends: $BUILD_DEPENDS
EOF

echo "Generated $OUTFILE (version $VERSION-$REVISION)" >&2
