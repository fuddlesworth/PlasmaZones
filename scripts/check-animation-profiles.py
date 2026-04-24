#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Two build-time integrity checks for PhosphorAnimation shipped data:

1. Every `profile: "..."` string referenced from QML has a corresponding
   shipped JSON file under data/profiles/.
2. Every `"curve": "..."` reference inside a shipped profile JSON resolves
   — either to a built-in curve type (bare 4-comma bezier, or
   `typeId:params` using a built-in typeId) or to a named curve under
   data/curves/.

Runs as a CMake custom-command at build time. Exits non-zero on any
typo so a missing data file fails the build rather than shipping and
silently falling back to library defaults at runtime.

## What this checker does NOT catch

The `profile:` regex only matches QUOTED STRING LITERALS — e.g.
`profile: "zone.highlight"`. It deliberately does not attempt to
resolve QML expressions:

  - `profile: condition ? "a" : "b"` — both branches silently skipped.
  - `profile: "shell." + Theme.variant` — concatenated identifiers
    skipped.
  - `profile: somePropertyAlias` — property references skipped.
  - `profile: Settings.primaryProfile` — property chains skipped.

Resolving these would require a full QML AST / type system, which is
well beyond the "CI tripwire for the 99% case" goal of this script.

The script also treats `data/profiles/` as authoritative: a profile
string with no corresponding file fails the build, even if it is
registered at runtime by a plugin-authored `PhosphorProfileRegistry::
registerProfile` call. Projects with plugin-registered profiles need
to either ship a placeholder JSON or extend `QML_DIRS` / the regex to
scope the check away from those sites.

Treat the checker as a lint, not a proof of correctness.

Usage:
    check-animation-profiles.py <source-root>
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

# QML Behavior sites we care about: `profile: "some.path"`. The regex
# tolerates any amount of whitespace between the colon and the quoted
# value and matches both single- and double-quoted forms so a future
# QML author using single quotes still gets caught.
#
# The `(?<![A-Za-z0-9_])` lookbehind anchors `profile` at an identifier
# boundary so a future lowercase property name like `subprofile: "..."`
# (or any `<word>profile:` binding — `audioprofile`, `tilingprofile`)
# isn't misread as a `profile:` reference and trip a confusing
# "missing data/profiles/<word>profile.json" build failure. CamelCase
# names like `colorProfile:` are already safe (the `P` breaks the
# lowercase substring match), but lowercase variants are not.
PROFILE_RE = re.compile(r"""(?<![A-Za-z0-9_])profile\s*:\s*["']([a-zA-Z0-9_.\-]+)["']""")

# Directories holding QML files that use PhosphorMotionAnimation. Keep
# the scan scope small so we don't wander into third-party imports or
# build artifacts.
QML_DIRS = [
    Path("src/ui"),
    Path("src/editor/qml"),
    Path("src/settings/qml"),
    Path("src/shared"),
]

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


def check_qml_profile_references(
    root: Path, shipped_profiles: set[str]
) -> dict[str, list[tuple[Path, int]]]:
    missing: dict[str, list[tuple[Path, int]]] = {}
    for rel in QML_DIRS:
        base = root / rel
        if not base.is_dir():
            continue
        for qml in base.rglob("*.qml"):
            try:
                text = qml.read_text(encoding="utf-8", errors="replace")
            except OSError as e:
                print(f"warning: cannot read {qml}: {e}", file=sys.stderr)
                continue
            for idx, line in enumerate(text.splitlines(), start=1):
                for match in PROFILE_RE.finditer(line):
                    name = match.group(1)
                    if name in shipped_profiles:
                        continue
                    missing.setdefault(name, []).append(
                        (qml.relative_to(root), idx)
                    )
    return missing


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
    if len(argv) != 2:
        print("usage: check-animation-profiles.py <source-root>", file=sys.stderr)
        return 2

    root = Path(argv[1]).resolve()
    profiles_dir = root / "data" / "profiles"
    curves_dir = root / "data" / "curves"

    if not profiles_dir.is_dir():
        print(f"error: profiles dir not found: {profiles_dir}", file=sys.stderr)
        return 1

    shipped_profiles = {p.stem for p in profiles_dir.glob("*.json")}
    shipped_curves = (
        {p.stem for p in curves_dir.glob("*.json")} if curves_dir.is_dir() else set()
    )

    rc = 0

    # Check 1: QML → profiles
    missing = check_qml_profile_references(root, shipped_profiles)
    if missing:
        rc = 1
        print(
            "error: QML references profile name(s) with no matching "
            "data/profiles/<name>.json:\n",
            file=sys.stderr,
        )
        for name in sorted(missing):
            print(f"  {name}:", file=sys.stderr)
            for path, lineno in missing[name]:
                print(f"    {path}:{lineno}", file=sys.stderr)
        print(
            "\nEither (a) add data/profiles/<name>.json with the desired "
            "curve/duration shape, or (b) fix the profile string to one "
            "of the shipped names.",
            file=sys.stderr,
        )

    # Check 2: profile.curve → built-in or data/curves/
    broken_curve_refs = check_profile_curve_references(profiles_dir, shipped_curves)
    if broken_curve_refs:
        rc = 1
        print(
            "\nerror: profile JSON(s) reference curve names that resolve "
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

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
