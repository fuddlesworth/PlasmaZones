#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build-time integrity check for PhosphorAnimation shipped data.

Validates that every `"curve": "..."` reference inside a shipped
profile JSON under data/profiles/ resolves — either to a built-in
curve type (bare 4-comma bezier, or `typeId:params` using a built-in
typeId) or to a named curve under data/curves/.

Runs as a CMake custom-command at build time. Exits non-zero on any
typo so a missing curve file fails the build rather than shipping and
silently falling back to library defaults at runtime.

## QML profile references are NOT validated

The shipped tree intentionally ships zero per-leaf profile JSONs:
animation timings/curves are driven entirely from the Settings UI via
`PhosphorProfileRegistry::registerProfile`, with parent-chain
inheritance (`PhosphorProfileRegistry::resolveWithInheritance`)
filling in any unset leaf from its nearest ancestor and ultimately
from `Profile::withDefaults()`. Because of that, every profile name
referenced from QML resolves at runtime regardless of which JSONs are
shipped — there's nothing for a build-time script to verify.

Usage:
    check-animation-profiles.py <source-root>
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Built-in curve typeIds recognised by CurveRegistry without a
# data/curves/ entry. Kept in sync with curveregistry.cpp's
# registerBuiltins — a typeId added there must be added here to keep
# this checker accepting it. (Small enough that drift is caught in
# review.)
BUILTIN_CURVE_TYPEIDS = frozenset([
    "bezier",
    "cubic-bezier",
    "spring",
    "elastic-in",
    "elastic-out",
    "elastic-in-out",
    "bounce-in",
    "bounce-out",
    "bounce-in-out",
])

BARE_BEZIER_RE = re.compile(
    r"""^\s*-?[0-9.]+\s*,\s*-?[0-9.]+\s*,\s*-?[0-9.]+\s*,\s*-?[0-9.]+\s*$"""
)


def curve_reference_resolves(spec: str, shipped_curves: set[str]) -> bool:
    """Replicate CurveRegistry::tryCreate's acceptance check.

    Accepts:
      - bare 4-comma float list (bezier wire format);
      - "<typeId>:<params>" where typeId is a built-in;
      - bare identifier that names either a built-in typeId or a shipped
        curve file (same lookup CurveRegistry performs for user curves
        registered via CurveLoader).
    """
    spec = spec.strip()
    if not spec:
        return False
    if BARE_BEZIER_RE.match(spec):
        return True
    colon = spec.find(":")
    if colon >= 0:
        typeid = spec[:colon].strip().lower()
        return typeid in BUILTIN_CURVE_TYPEIDS or typeid in shipped_curves
    # Bare identifier: either a built-in typeId (no params) or a named
    # curve registered by CurveLoader.
    return spec in BUILTIN_CURVE_TYPEIDS or spec in shipped_curves


def check_profile_curve_references(
    profiles_dir: Path, shipped_curves: set[str]
) -> list[tuple[Path, str]]:
    broken: list[tuple[Path, str]] = []
    for profile_path in sorted(profiles_dir.glob("*.json")):
        try:
            data = json.loads(profile_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            broken.append((profile_path, f"could not parse: {e}"))
            continue
        if not isinstance(data, dict):
            broken.append((profile_path, "root is not a JSON object"))
            continue
        spec = data.get("curve")
        if spec is None:
            # No curve field — library default applies. Fine.
            continue
        if not isinstance(spec, str):
            broken.append((profile_path, f"'curve' is not a string: {spec!r}"))
            continue
        if not curve_reference_resolves(spec, shipped_curves):
            broken.append((profile_path, f"unresolved curve reference {spec!r}"))
    return broken


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="check-animation-profiles.py",
        description="PhosphorAnimation shipped-data integrity check.",
    )
    parser.add_argument("source_root", type=Path, help="Repository root to scan.")
    args = parser.parse_args(argv[1:])

    root = args.source_root.resolve()
    profiles_dir = root / "data" / "profiles"
    curves_dir = root / "data" / "curves"

    # An absent profiles dir is fine — the shipped tree may legitimately
    # ship zero per-leaf JSONs and rely entirely on Settings-driven
    # registry entries with parent-chain inheritance. Treat as "no
    # JSONs to validate" rather than a hard error.
    shipped_curves = (
        {p.stem for p in curves_dir.glob("*.json")} if curves_dir.is_dir() else set()
    )

    if not profiles_dir.is_dir():
        return 0

    broken_curve_refs = check_profile_curve_references(profiles_dir, shipped_curves)
    if not broken_curve_refs:
        return 0

    print(
        "error: profile JSON(s) reference curve names that resolve "
        "neither to a built-in typeId nor to a shipped "
        "data/curves/<name>.json:\n",
        file=sys.stderr,
    )
    for path, reason in broken_curve_refs:
        print(f"  {path.relative_to(root)}: {reason}", file=sys.stderr)
    print(
        "\nEither (a) add data/curves/<name>.json defining the curve, "
        "or (b) fix the spec to a built-in typeId (bezier / spring / "
        "elastic-in-out / bounce-out / ...) or bare-bezier wire form.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
