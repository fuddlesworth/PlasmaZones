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
to either ship a placeholder JSON or extend the scan scope / the
regex to scope the check away from those sites.

Treat the checker as a lint, not a proof of correctness.

Usage:
    check-animation-profiles.py <source-root> [--exclude-dir DIR]...
                                              [--qml-dir DIR]...
"""

from __future__ import annotations

import argparse
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
#
# Two regexes: the "valid" form requires at least one profile-name
# character so it can be validated against shipped files; the "empty"
# form catches `profile: ""` (or single-quoted equivalent) which is
# always a mistake — a no-op Behavior whose intended animation name
# was never filled in.
PROFILE_RE = re.compile(r"""(?<![A-Za-z0-9_])profile\s*:\s*["']([a-zA-Z0-9_.\-]+)["']""")
PROFILE_EMPTY_RE = re.compile(r"""(?<![A-Za-z0-9_])profile\s*:\s*(""|'')""")

# Default QML source roots — used when the caller doesn't pass any
# explicit --qml-dir arguments. Auto-discovery walks `src/**/*.qml`
# under the source root so a new QML module dropped under `src/…`
# doesn't require editing this file just to get lint coverage.
DEFAULT_QML_ROOTS = [Path("src")]

# Directories we always skip during the walk — build artifacts, VCS
# metadata, and cached third-party imports. Extended via
# `--exclude-dir` on the command line.
DEFAULT_EXCLUDE_DIRS = frozenset({"build", ".git", ".cache", "node_modules"})

# Import marker — a file without `import org.phosphor.animation` (or
# the sub-module form) is very unlikely to be using
# PhosphorMotionAnimation. The context anchor below uses this to avoid
# false positives from unrelated `ListElement { profile: "x" }` sites
# in non-animation QML modules.
ANIMATION_IMPORT_RE = re.compile(r"^\s*import\s+org\.phosphor\.animation(?:\s|$)", re.MULTILINE)

# Context anchor — `profile:` references that appear within a
# PhosphorMotionAnimation block are always relevant. The regex is a
# deliberately simple "nearest preceding PhosphorMotionAnimation {"
# check: full brace-balanced parsing would require a real QML parser,
# but for the patterns this codebase actually emits (single-line
# Behavior → PhosphorMotionAnimation { ... } blocks, no nesting of
# PhosphorMotionAnimation inside another PhosphorMotionAnimation) the
# "is the nearest preceding open brace owned by PhosphorMotionAnimation"
# heuristic is exact.
PHOSPHOR_MOTION_BLOCK_RE = re.compile(r"\bPhosphorMotionAnimation\s*\{")

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


def strip_comments(text: str) -> str:
    """Remove `//` line comments and `/* ... */` block comments from QML.

    A simple state machine — no regex games, because block comments
    can span multiple lines and a line comment inside a string
    literal is not a comment. We track:

      - whether we're inside a double- or single-quoted string;
      - whether we're inside a `/* */` block comment;
      - whether we're inside a `//` line comment (terminated by LF).

    Comment spans are replaced with a SPACE (not removed) so that
    line numbers and column offsets are preserved in the return value
    — the `finditer` caller below reports line numbers in diagnostics
    and would otherwise point at the wrong line in a file with block
    comments.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    in_line_comment = False
    in_block_comment = False
    in_string: str | None = None  # either None, '"', or "'"

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if in_line_comment:
            # Line comment ends at '\n' — emit the newline, stay on
            # line boundary for the next iteration.
            if c == "\n":
                in_line_comment = False
                out.append("\n")
            else:
                # Preserve the character's column space with a space;
                # keep tabs as tabs so any leading-whitespace
                # diagnostics remain accurate.
                out.append(" " if c != "\t" else "\t")
            i += 1
            continue

        if in_block_comment:
            if c == "*" and nxt == "/":
                in_block_comment = False
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " " if c != "\t" else "\t")
            i += 1
            continue

        if in_string is not None:
            # Inside string literal — just pass the character through.
            # Handle escape sequences so a `\"` inside a string doesn't
            # prematurely close the string.
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == in_string:
                in_string = None
            i += 1
            continue

        # Not inside string or comment.
        if c == "/" and nxt == "/":
            in_line_comment = True
            out.append("  ")
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block_comment = True
            out.append("  ")
            i += 2
            continue
        if c == '"' or c == "'":
            in_string = c
            out.append(c)
            i += 1
            continue

        out.append(c)
        i += 1

    return "".join(out)


def find_motion_animation_spans(text: str) -> list[tuple[int, int]]:
    """Return [(start, end), ...] offsets of each top-level
    PhosphorMotionAnimation { ... } block.

    Scans for the opening `PhosphorMotionAnimation {` and walks the
    brace-balanced region to its matching close brace. Strings and
    comments were already stripped by `strip_comments`, so a simple
    depth counter is sufficient.
    """
    spans: list[tuple[int, int]] = []
    for open_match in PHOSPHOR_MOTION_BLOCK_RE.finditer(text):
        start = open_match.end() - 1  # points at the '{'
        depth = 1
        i = start + 1
        n = len(text)
        while i < n and depth > 0:
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    spans.append((open_match.start(), i + 1))
                    break
            i += 1
        else:
            # Unterminated block — take whatever we've got. Treat as
            # running to EOF so any `profile:` binding below the open
            # still counts towards the check.
            spans.append((open_match.start(), n))
    return spans


def offset_in_spans(offset: int, spans: list[tuple[int, int]]) -> bool:
    for s, e in spans:
        if s <= offset < e:
            return True
    return False


def iter_qml_files(root: Path, qml_dirs: list[Path], exclude_dirs: set[str]):
    """Yield absolute paths to QML files under the configured roots.

    If `qml_dirs` contains absolute paths, use them as given. Relative
    paths are resolved against `root`. Directories under an excluded
    name (anywhere in the path) are pruned.
    """
    seen: set[Path] = set()
    for rel in qml_dirs:
        base = rel if rel.is_absolute() else (root / rel)
        if not base.is_dir():
            continue
        for qml in base.rglob("*.qml"):
            # Prune anywhere along the path — rglob doesn't let us
            # skip dirs mid-walk cheaply, so we filter post-hoc.
            if any(part in exclude_dirs for part in qml.parts):
                continue
            resolved = qml.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            yield qml


class QmlProfileDiagnostic:
    __slots__ = ("kind", "name", "path", "line")

    def __init__(self, kind: str, name: str, path: Path, line: int) -> None:
        self.kind = kind
        self.name = name
        self.path = path
        self.line = line


def check_qml_profile_references(
    root: Path,
    shipped_profiles: set[str],
    qml_dirs: list[Path],
    exclude_dirs: set[str],
) -> tuple[dict[str, list[tuple[Path, int]]], list[QmlProfileDiagnostic]]:
    """Scan QML files for `profile:` bindings.

    Returns `(missing, diagnostics)` where:
      - `missing` is the per-name index of unresolved profile
        references (same shape as before).
      - `diagnostics` is a flat list of additional findings — currently
        just empty-string profile bindings (`profile: ""`), which are
        always a mistake and reported distinctly from missing-file
        errors.

    The scan uses:
      1. Comment-stripped text (so `// profile: "x"` doesn't false-
         positive).
      2. A context anchor — `profile:` is only reported if either the
         binding falls inside a `PhosphorMotionAnimation { ... }` block
         OR the file has `import org.phosphor.animation`. This lets
         non-animation QML use `profile:` for unrelated ListElement
         fields without tripping the linter.
    """
    missing: dict[str, list[tuple[Path, int]]] = {}
    diagnostics: list[QmlProfileDiagnostic] = []
    for qml in iter_qml_files(root, qml_dirs, exclude_dirs):
        try:
            raw = qml.read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"warning: cannot read {qml}: {e}", file=sys.stderr)
            continue

        text = strip_comments(raw)
        motion_spans = find_motion_animation_spans(text)
        has_import = bool(ANIMATION_IMPORT_RE.search(text))

        def _reportable(offset: int) -> bool:
            # A `profile:` match is reportable when it's inside a
            # PhosphorMotionAnimation block, OR when the file imports
            # org.phosphor.animation (the latter catches cases where
            # the binding is passed through a property alias or set
            # imperatively on a PhosphorMotionAnimation instance).
            return offset_in_spans(offset, motion_spans) or has_import

        rel: Path
        try:
            rel = qml.relative_to(root)
        except ValueError:
            rel = qml
        for match in PROFILE_RE.finditer(text):
            if not _reportable(match.start()):
                continue
            name = match.group(1)
            if name in shipped_profiles:
                continue
            # Compute the 1-based line number from the offset.
            line = text.count("\n", 0, match.start()) + 1
            missing.setdefault(name, []).append((rel, line))

        for match in PROFILE_EMPTY_RE.finditer(text):
            if not _reportable(match.start()):
                continue
            line = text.count("\n", 0, match.start()) + 1
            diagnostics.append(QmlProfileDiagnostic("empty-profile", "", rel, line))

    return missing, diagnostics


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
    parser.add_argument(
        "--qml-dir",
        dest="qml_dirs",
        action="append",
        default=None,
        type=Path,
        help=(
            "Explicit QML source root (relative to source-root or absolute). "
            "May be passed multiple times. If omitted, auto-discovers under "
            "src/."
        ),
    )
    parser.add_argument(
        "--exclude-dir",
        dest="exclude_dirs",
        action="append",
        default=[],
        help="Additional directory basename to skip during QML walk.",
    )
    args = parser.parse_args(argv[1:])

    root = args.source_root.resolve()
    profiles_dir = root / "data" / "profiles"
    curves_dir = root / "data" / "curves"

    if not profiles_dir.is_dir():
        print(f"error: profiles dir not found: {profiles_dir}", file=sys.stderr)
        return 1

    shipped_profiles = {p.stem for p in profiles_dir.glob("*.json")}
    shipped_curves = (
        {p.stem for p in curves_dir.glob("*.json")} if curves_dir.is_dir() else set()
    )

    qml_dirs: list[Path] = args.qml_dirs if args.qml_dirs else list(DEFAULT_QML_ROOTS)
    exclude_dirs: set[str] = set(DEFAULT_EXCLUDE_DIRS) | set(args.exclude_dirs)

    rc = 0

    # Check 1: QML → profiles (plus empty-string diagnostics).
    missing, diagnostics = check_qml_profile_references(
        root, shipped_profiles, qml_dirs, exclude_dirs
    )
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

    if diagnostics:
        rc = 1
        print(
            "\nerror: QML contains `profile: \"\"` (empty-string) binding(s) — "
            "a no-op Behavior whose animation name was never filled in:\n",
            file=sys.stderr,
        )
        for diag in diagnostics:
            print(f"  {diag.path}:{diag.line}", file=sys.stderr)

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
