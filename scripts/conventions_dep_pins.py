# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""The dep-pins rule: every packaging recipe carries the third-party pins exactly.

cmake/PhosphorDependencies.cmake is the only place a third-party archive is pinned
(version, URL, file name, SHA-256). An offline packaging build cannot download, so
each recipe in PIN_RECIPES lists the archives as extra sources of its own, and in
doing so repeats the URL and the hash. Nothing else ties those copies to the pin, so
a bump that missed one recipe would only surface when that distro's build failed its
hash check, or worse, built from whatever the recipe still named. This rule fails the
gate instead, naming the recipe and the dependency.

The jobs that stage the archives with the fetch script (the Debian and OBS paths in
release.yml, CI's luau-analyze) read the pin file itself and carry no copy, so they
are not listed.

Stdlib only, and it returns plain tuples rather than the gate's Violation so the two
files cannot form a cycle, the same shape as conventions_shared_text.py.
"""
from __future__ import annotations

import base64
import re
from pathlib import Path

PIN_FILE = "cmake/PhosphorDependencies.cmake"

# Every recipe that fetches the archives itself and so must repeat each pin.
PIN_RECIPES = (
    "packaging/arch/PKGBUILD",
    "packaging/arch/PKGBUILD.git",
    "packaging/rpm/plasmazones.spec",
    "packaging/nix/package.nix",
)

# One `set(NAME "value")` per line, the exact shape the pin file uses. Held strict on
# purpose: CI's luau-analyze job reads the Luau pin with a sed of the same shape, so a
# reformatted pin that this regex stops matching would also break that job, and this
# rule says so first.
_SET = re.compile(r'^set\((PHOSPHOR_DEP_([A-Za-z0-9_]+?)_(VERSION|URL|FILE|SHA256)) "([^"]*)"\)\s*$', re.M)
_DEPS = re.compile(r"^set\(PHOSPHOR_DEPS ([^)]*)\)\s*$", re.M)
_HEX64 = re.compile(r"^[0-9a-f]{64}$")


def read_pins(text: str) -> tuple[dict[str, dict[str, str]], list[str]]:
    """Parse the pin file. Returns ({dep: {field: value}}, [problem])."""
    problems: list[str] = []
    m = _DEPS.search(text)
    if not m:
        return {}, ["no `set(PHOSPHOR_DEPS ...)` line, so the rule cannot tell which archives are pinned"]
    names = m.group(1).split()
    if not names:
        problems.append("PHOSPHOR_DEPS is empty, so this rule checks nothing")
    fields: dict[str, dict[str, str]] = {n: {} for n in names}
    for _, dep, field, value in _SET.findall(text):
        if dep in fields:
            fields[dep][field] = value
    for dep, got in fields.items():
        missing = [f for f in ("VERSION", "URL", "FILE", "SHA256") if f not in got]
        if missing:
            problems.append(f"{dep} is in PHOSPHOR_DEPS but has no {', '.join(missing)} pin "
                            f"in the one-line `set(PHOSPHOR_DEP_{dep}_<FIELD> \"...\")` shape")
        elif not _HEX64.match(got["SHA256"]):
            problems.append(f"{dep}'s SHA256 pin is not 64 lowercase hex digits")
    return fields, problems


def dep_pin_problems(repo: Path) -> list[tuple[str, str]]:
    """Return (repo-relative path, message) for each recipe that drifted from a pin.

    `repo` is passed in and every path is anchored on it, never on the process CWD,
    so the rule cannot pass in silence when run from another directory.
    """
    out: list[tuple[str, str]] = []
    pin_path = repo / PIN_FILE
    try:
        text = pin_path.read_text(encoding="utf-8")
    except OSError as e:
        return [(PIN_FILE, f"cannot read the pin file ({e.strerror}), so no recipe was checked")]
    pins, problems = read_pins(text)
    out.extend((PIN_FILE, p) for p in problems)
    # An empty recipe list is the quiet way to switch the rule off while it stays in
    # --list-rules, so it has to announce itself.
    if not PIN_RECIPES:
        out.append(("scripts/conventions_dep_pins.py",
                    "PIN_RECIPES is empty, so this rule checks nothing; delete the rule outright "
                    "rather than emptying its list"))
    for recipe in PIN_RECIPES:
        try:
            body = (repo / recipe).read_text(encoding="utf-8")
        except OSError as e:
            out.append((recipe, f"listed in PIN_RECIPES but cannot be read ({e.strerror})"))
            continue
        for dep, got in pins.items():
            if "URL" not in got or "SHA256" not in got:
                continue
            if got["URL"] not in body:
                out.append((recipe, f"does not fetch {dep} from its pinned URL {got['URL']} "
                                    f"({PIN_FILE})"))
            if got["SHA256"] not in body:
                out.append((recipe, f"does not carry {dep}'s pinned SHA-256 {got['SHA256']} "
                                    f"({PIN_FILE})"))
            # Nix verifies the SRI form, not the hex one the rule finds in its comment,
            # so a .nix recipe has to carry both and they must be the same digest.
            if recipe.endswith(".nix") and _HEX64.match(got["SHA256"]):
                sri = "sha256-" + base64.b64encode(bytes.fromhex(got["SHA256"])).decode()
                if sri not in body:
                    out.append((recipe, f"does not carry {dep}'s pinned hash in the SRI form Nix "
                                        f"checks ({sri})"))
    return out
