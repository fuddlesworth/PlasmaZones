#!/bin/bash
# Update AUR PKGBUILD with new version and hash
# Usage: ./update-aur.sh <version> <sha256>
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

VERSION="${1:-}"
SHA256="${2:-}"
PKGBUILD="${3:-PKGBUILD}"

if [[ -z "$VERSION" || -z "$SHA256" ]]; then
    echo "Usage: $0 <version> <sha256> [PKGBUILD]"
    echo "Example: $0 1.3.4 abc123..."
    exit 1
fi

if [[ ! -f "$PKGBUILD" ]]; then
    echo "Error: PKGBUILD not found: $PKGBUILD"
    exit 1
fi

echo "Updating $PKGBUILD to version $VERSION"

# Update pkgver
sed -i "s/^pkgver=.*/pkgver=$VERSION/" "$PKGBUILD"

# Update the package hash (first sha256sum entry)
# This handles both single-line and the first entry of multi-line sha256sums
sed -i "0,/^sha256sums=(/s/sha256sums=([^)]*)/sha256sums=(\n    '$SHA256'/" "$PKGBUILD" 2>/dev/null || \
sed -i "s/^\s*'[a-f0-9]\{64\}'/    '$SHA256'/" "$PKGBUILD"

# Reset pkgrel to 1 for new version
sed -i "s/^pkgrel=.*/pkgrel=1/" "$PKGBUILD"

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
