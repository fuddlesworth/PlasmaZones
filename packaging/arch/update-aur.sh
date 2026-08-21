#!/bin/bash
# Update AUR PKGBUILD with new version and hash
# Usage: ./update-aur.sh <version> <sha256> [PKGBUILD] [pkgrel]
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

VERSION="${1:-}"
SHA256="${2:-}"
PKGBUILD="${3:-PKGBUILD}"
# A new version always starts at pkgrel=1. Raise it for a rebuild-only
# republish of a version that is already out: same source, new package.
# The case that forces this is an ABI break under us — the layer-shell QPA
# plugin compiles against Qt private headers (QtWaylandClient/private/...),
# whose class layouts carry no ABI guarantee, so a qt6-base/qt6-wayland
# patch bump can leave an installed build reading members at stale offsets.
PKGREL="${4:-1}"

if [[ -z "$VERSION" || -z "$SHA256" ]]; then
    echo "Usage: $0 <version> <sha256> [PKGBUILD] [pkgrel]"
    echo "Example: $0 1.3.4 abc123..."
    echo "Example: $0 1.3.4 abc123... PKGBUILD 2   # rebuild-only republish"
    exit 1
fi

if [[ ! "$PKGREL" =~ ^[0-9]+$ ]]; then
    echo "Error: pkgrel must be a positive integer, got: $PKGREL"
    exit 1
fi

if [[ ! -f "$PKGBUILD" ]]; then
    echo "Error: PKGBUILD not found: $PKGBUILD"
    exit 1
fi

echo "Updating $PKGBUILD to version $VERSION-$PKGREL"

# Update pkgver
sed -i "s/^pkgver=.*/pkgver=$VERSION/" "$PKGBUILD"

# Update the package hash. Anchored on the sha256sums=(...) array rather
# than on a bare hash-shaped line, and it collapses the array to the single
# entry the source= array carries, so a stale second hash can't survive. Same
# rewrite the release workflow's AUR publish steps use — keep the two in step.
# The previous sed pair dropped the array's closing paren on the single-line
# form, leaving a PKGBUILD that makepkg could not source at all.
awk -v sum="$SHA256" '
  # Single-line form: sha256sums=(<hash>) — replace inline.
  /^sha256sums=\(.*\)$/ {
    print "sha256sums=(\047" sum "\047)"
    next
  }
  # Multi-line form: opening `sha256sums=(` alone, entries, then `)` alone.
  /^sha256sums=\(/ {
    print "sha256sums=("
    print "    \047" sum "\047"
    print ")"
    in_block = 1
    next
  }
  in_block && /^\)/ { in_block = 0; next }
  in_block { next }
  { print }
' "$PKGBUILD" > "$PKGBUILD.new" && mv "$PKGBUILD.new" "$PKGBUILD"

sed -i "s/^pkgrel=.*/pkgrel=$PKGREL/" "$PKGBUILD"

echo "Updated $PKGBUILD:"
grep -E "^(pkgver|pkgrel|sha256sums)" "$PKGBUILD" | head -5

# Generate .SRCINFO
if command -v makepkg &> /dev/null; then
    echo "Generating .SRCINFO..."
    makepkg --printsrcinfo > .SRCINFO
    echo "Done!"
else
    echo "Warning: makepkg not found, .SRCINFO not generated"
    echo "Run 'makepkg --printsrcinfo > .SRCINFO' manually"
fi
