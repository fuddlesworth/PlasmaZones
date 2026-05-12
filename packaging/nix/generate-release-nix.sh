#!/usr/bin/env bash
# Transform packaging/nix/package.nix (a generic derivation that takes `src`
# as a parameter and computes the version dynamically) into a release file
# that bakes in a specific tag's fetchFromGitHub source + SRI hash.
#
# Output is written to ./plasmazones.nix in the current directory so the
# caller can upload it as a release asset.
#
# Usage:
#   ./generate-release-nix.sh <version>   # e.g. 3.0.0
#
# Smoke-tested by release.yml's nix job before being shipped as a release
# artifact — if the awk pattern below ever falls out of sync with the
# upstream package.nix shape, that step fails the build instead of
# silently uploading garbage.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

if [[ $# -lt 1 || -z "${1:-}" ]]; then
    echo "Usage: $0 <version>" >&2
    exit 2
fi

VERSION="$1"
TARBALL="https://github.com/fuddlesworth/PlasmaZones/archive/refs/tags/v${VERSION}.tar.gz"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
SRC_NIX="$PROJECT_DIR/packaging/nix/package.nix"
OUT_NIX="${OUT_NIX:-$(pwd)/plasmazones.nix}"

if [[ ! -f "$SRC_NIX" ]]; then
    echo "Error: $SRC_NIX not found" >&2
    exit 1
fi

echo "Fetching source hash for $TARBALL" >&2
NIX_HASH=$(nix-prefetch-url --unpack "$TARBALL" 2>/dev/null)
SRI_HASH=$(nix hash to-sri --type sha256 "$NIX_HASH")
echo "SRI hash: $SRI_HASH" >&2

awk -v version="$VERSION" -v hash="$SRI_HASH" '
    NR == 1 {
        print "# PlasmaZones " version " — Nix package"
        print "# SPDX-License-Identifier: GPL-3.0-or-later"
        print "#"
        print "# Usage:"
        print "#   environment.systemPackages = ["
        print "#     (pkgs.callPackage ./plasmazones.nix {})"
        print "#   ];"
        next
    }
    !started && /^#/ { next }
    !started && /^$/ { next }
    /^\{/ { started = 1 }
    started && /^  src,$/ { print "  fetchFromGitHub,"; next }
    /^let$/ { in_let = 1; next }
    in_let && /^in$/ { in_let = 0; next }
    in_let { next }
    /version = extractVersion src;/ {
        print "  version = \"" version "\";"
        next
    }
    /inherit src;/ {
        print "  src = fetchFromGitHub {"
        print "    owner = \"fuddlesworth\";"
        print "    repo = \"PlasmaZones\";"
        print "    rev = \"v" version "\";"
        print "    hash = \"" hash "\";"
        print "  };"
        next
    }
    !in_let { print }
' "$SRC_NIX" > "$OUT_NIX"

# Smoke check: confirm the result still parses as Nix. If `nix-instantiate`
# is on PATH, run it and fail loudly on parse errors. Without nix-instantiate
# we at least verify the file is non-empty.
if [[ ! -s "$OUT_NIX" ]]; then
    echo "Error: generated $OUT_NIX is empty" >&2
    exit 1
fi
if command -v nix-instantiate >/dev/null 2>&1; then
    nix-instantiate --parse "$OUT_NIX" >/dev/null
    echo "✓ $OUT_NIX parses cleanly" >&2
fi

echo "Wrote $OUT_NIX" >&2
