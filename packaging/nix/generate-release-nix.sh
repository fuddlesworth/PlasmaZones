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
# Don't muzzle stderr — surface curl / nix-prefetch-url failures (404 on
# a missing tag, network errors). `set -e` aborts on nix-prefetch-url's
# non-zero exit; the explicit empty-check is belt-and-suspenders for the
# bizarre case where it succeeds-with-empty-output.
NIX_HASH=$(nix-prefetch-url --unpack "$TARBALL")
if [[ -z "$NIX_HASH" ]]; then
    echo "Error: nix-prefetch-url returned empty hash for $TARBALL" >&2
    exit 1
fi
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

# Smoke checks:
#  1. Non-empty file (catches awk swallowing everything).
#  2. Required attributes present (catches package.nix refactors that drop
#     the patterns we rewrite — `inherit src;` / `version = extractVersion`
#     — without changing the upstream file's syntax).
#  3. `nix-instantiate --parse` if available (catches Nix syntax errors).
if [[ ! -s "$OUT_NIX" ]]; then
    echo "Error: generated $OUT_NIX is empty" >&2
    exit 1
fi
if ! grep -q 'fetchFromGitHub' "$OUT_NIX"; then
    echo "Error: generated $OUT_NIX is missing fetchFromGitHub — upstream package.nix may have been restructured" >&2
    exit 1
fi
if ! grep -qE '^[[:space:]]*version = "' "$OUT_NIX"; then
    echo "Error: generated $OUT_NIX is missing a hardcoded version attribute" >&2
    exit 1
fi
if command -v nix-instantiate >/dev/null 2>&1; then
    nix-instantiate --parse "$OUT_NIX" >/dev/null
    echo "✓ $OUT_NIX parses cleanly" >&2
fi

echo "Wrote $OUT_NIX" >&2
