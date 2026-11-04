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

if [[ ! "$PKGREL" =~ ^[1-9][0-9]*$ ]]; then
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
# than on a bare hash-shaped line, and it rewrites only the FIRST entry, which
# is the release tarball's (source[0]). The source PKGBUILDs list the pinned
# dependency archives after it, each with its literal hash that
# check-conventions.py holds to cmake/PhosphorDependencies.cmake, so those
# entries pass through untouched. Same rewrite the release workflow's AUR
# publish steps use, so keep the two in step. The previous sed pair dropped the
# array's closing paren on the single-line form, leaving a PKGBUILD that makepkg
# could not source at all.
#
# Two separate commands, not `awk ... && mv ...`: under `set -e` a failure on
# the left of && does not stop the script, so a PKGBUILD the rewrite could not
# parse went on to the pkgrel bump and the .SRCINFO step with its old hash.
awk -v sum="$SHA256" '
  # Single-line form with one entry: sha256sums=(<hash>), replaced inline.
  !done && /^sha256sums=\(\047[^\047]*\047\)$/ {
    print "sha256sums=(\047" sum "\047)"
    done = 1
    next
  }
  # Multi-line form: `sha256sums=(` alone, one entry per line, `)` alone.
  # Only the first entry is rewritten.
  !done && /^sha256sums=\($/ { in_block = 1; print; next }
  in_block && /^\)/ { in_block = 0; print; next }
  in_block && !done && /^[[:space:]]*\047[^\047]*\047[[:space:]]*(#.*)?$/ {
    print "    \047" sum "\047"
    done = 1
    next
  }
  { print }
  END {
    if (!done) {
      print "sha256sums: no first entry found to rewrite" > "/dev/stderr"
      exit 1
    }
  }
' "$PKGBUILD" > "$PKGBUILD.new"
mv "$PKGBUILD.new" "$PKGBUILD"

# Every source needs its own hash, or makepkg refuses the PKGBUILD. Sourced in
# a subshell: a PKGBUILD's top level is assignments only.
# shellcheck disable=SC1090
if ! (source "$PKGBUILD" && [[ ${#source[@]} -eq ${#sha256sums[@]} && "${sha256sums[0]}" == "$SHA256" ]]); then
    echo "Error: $PKGBUILD has a sha256sums array that does not match its source array after the rewrite" >&2
    exit 1
fi

sed -i "s/^pkgrel=.*/pkgrel=$PKGREL/" "$PKGBUILD"

# The binary PKGBUILD downloads from a named GitHub release. A revision
# beyond 1 lives on its own v<version>-r<N> release (a published release is
# immutable and cannot take new assets), so point it there.
if grep -q '^_release=' "$PKGBUILD"; then
    if [[ "$PKGREL" == "1" ]]; then
        sed -i "s/^_release=.*/_release=v$VERSION/" "$PKGBUILD"
    else
        sed -i "s/^_release=.*/_release=v$VERSION-r$PKGREL/" "$PKGBUILD"
    fi
fi

echo "Updated $PKGBUILD:"
grep -E "^(pkgver|pkgrel|_release|sha256sums)" "$PKGBUILD" | head -6

# Generate .SRCINFO
if command -v makepkg &> /dev/null; then
    echo "Generating .SRCINFO..."
    # -p, because every other step honours "$PKGBUILD" and a bare makepkg
    # reads ./PKGBUILD. Called as documented with PKGBUILD.bin or
    # PKGBUILD.git, this generated .SRCINFO from a different file, or
    # errored under set -e with .SRCINFO already truncated by the >.
    makepkg -p "$PKGBUILD" --printsrcinfo > .SRCINFO
    echo "Done!"
else
    echo "Warning: makepkg not found, .SRCINFO not generated"
    echo "Run 'makepkg --printsrcinfo > .SRCINFO' manually"
fi
