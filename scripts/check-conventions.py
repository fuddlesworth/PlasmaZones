#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Mechanical enforcement of the CLAUDE.md conventions that are grep-decidable.

These rules were previously enforced only by reviewer attention, which means
they were caught (if at all) during an audit pass rather than at the moment the
violation was written. Every rule here is a ratchet: the tree currently passes,
so any failure is a genuine regression introduced by the change under review.

The one rule with real historical drift is the file-size ceiling. Roughly five
dozen files already exceed it, so that rule is enforced against a recorded
baseline (scripts/oversize-baseline.json): existing overruns are tolerated at
their recorded length, growing one is a failure, and a new file over the
ceiling is a failure. That matches the CLAUDE.md wording, which grandfathers
the existing overruns but treats growth as a finding.

Usage:
    scripts/check-conventions.py               # check the whole tree
    scripts/check-conventions.py FILE...       # check only these files
    scripts/check-conventions.py --rules R,R   # run only the named rules
    scripts/check-conventions.py --staged F... # the pre-commit form lefthook invokes
    scripts/check-conventions.py --selftest    # the form CI invokes beside the gate
    scripts/check-conventions.py --list-rules
    scripts/check-conventions.py --update-baseline

Exit status is 1 if any violation was found, 0 otherwise.
"""

from __future__ import annotations

import argparse
import codecs
import json
import re
import string
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def _add_script_dir_to_path() -> None:
    """Make this file's directory importable for its sibling modules.

    Idempotent, because a bare insert grew sys.path on every call.
    """
    d = str(Path(__file__).resolve().parent)
    if d not in sys.path:
        sys.path.insert(0, d)


BASELINE = REPO / "scripts" / "oversize-baseline.json"

# CLAUDE.md: under 1000 is the target, 1000-1150 is tolerated, past 1150 split.
# Only the hard ceiling is machine-enforced; the soft target is a review matter.
SIZE_CEILING = 1150


@dataclass
class Violation:
    rule: str
    path: str
    line: int
    message: str

    def format(self) -> str:
        where = f"{self.path}:{self.line}" if self.line else self.path
        return f"{where}: [{self.rule}] {self.message}"


# --------------------------------------------------------------------------
# Source helpers
# --------------------------------------------------------------------------

CPP_SUFFIXES = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp"}
QML_SUFFIXES = {".qml"}
SHADER_SUFFIXES = {".frag", ".vert", ".glsl"}
# .js is here so rule_spdx and rule_license cover the 17 QML .js libraries. .sh/.cmake/.spec/
# .desktop joined once every tracked one carried a head header, and .c once the two QPA protocol
# stubs were found to be the only comment-bearing C sources no rule read. THREE rules read this
# set, and so does --update-baseline, so .spec also entered the size ratchet. 26 of the 40 tracked
# suffix VALUES are still outside it (737 of 3830 files), not just the .in/.xml/.txt and data JSON
# an earlier version of this comment named, .md and .yml among them.
CODE_SUFFIXES = (CPP_SUFFIXES | QML_SUFFIXES | SHADER_SUFFIXES
                 | {".luau", ".py", ".js", ".sh", ".cmake", ".spec", ".desktop"})

# Which rules this invocation is running. rule_license consults it so it only
# defers a missing header to the spdx rule when that rule will actually run.
_SELECTED_RULES: set[str] = set()

# Trees that are vendored or generated and are not ours to police. The
# vendored tree is phosphor-libs/extern/ since the tier split; a bare "extern/"
# matches nothing and would quietly start policing anything vendored there.
EXCLUDED_PREFIXES = ("phosphor-libs/extern/", "build/", "build-off/", "build-nounity/",
                     "build-release/", "build-relwithdebinfo/")


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files"], cwd=REPO, capture_output=True, text=True, check=True
    ).stdout.split("\n")
    # is_file() because `git ls-files` still lists a file deleted without `git rm`, and
    # read() raises on it, taking six of the nine rules down with a traceback.
    return [f for f in out if f and not f.startswith(EXCLUDED_PREFIXES) and (REPO / f).is_file()]


def read(path: str) -> str:
    # Never raises. An unreadable tracked file (no read permission, or a path that is not
    # a regular file) used to take the whole gate down with a traceback from whichever
    # rule reached it first, which reads like a broken gate rather than a dirty tree.
    #
    # "" is a fallback, not a diagnosis. main() drops unreadable paths before any rule
    # sees them and reports the real cause, because "" makes every rule downstream of it
    # lie: prose calls well-formed JSON malformed, spdx and license call a file that has
    # a header headerless, and file-size sees zero lines. Returning "" still matters for
    # a path a rule opens on its own, such as a companion header it names, where the
    # cost of an empty string is a missed check rather than a false one.
    try:
        return (REPO / path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def read_error(path: str, *, repo: Path | None = None) -> str | None:
    """The reason a path cannot be read as text, or None when it can be.

    `repo` overrides the tree, which the self-test needs: pinning this against a real
    tracked file would mean chmod-ing one during a pre-commit hook."""
    try:
        ((repo or REPO) / path).read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return exc.strerror or exc.__class__.__name__
    return None


def partition_readable(files: list[str], *, repo: Path | None = None) -> tuple[list[str], list[Violation]]:
    """Split FILES into the ones a rule can read, and one violation per path it cannot.

    A path that cannot be read gets one finding naming why, and is then kept away from
    the rules. Handing them "" instead made them answer confidently about a file none of
    them had seen: "malformed JSON" on well-formed JSON, "missing SPDX header" on a file
    that carries one. This is a PRECONDITION of the gate rather than a convention, so it
    is not in RULES and --rules cannot switch it off; a subset run that could re-admit
    unreadable paths would bring the false findings back with it. Two rules DO reach past
    this list by design: dep5 says why at its widening, the pack glob in its OSError arm.

    A function rather than a loop inside main() so it can be pinned: as inline code its
    only caller was main(), which no test invokes, and neutering it left the self-test
    green. `repo` is here for the same reason read_error's is.
    """
    readable: list[str] = []
    problems: list[Violation] = []
    for f in files:
        err = read_error(f, repo=repo)
        if err is None:
            readable.append(f)
        else:
            problems.append(Violation("unreadable", f, 0, f"cannot be read ({err}), so no rule could check it"))
    return readable, problems


def strip_c_comments(text: str) -> str:
    """Blank out // and /* */ comments, preserving line structure and offsets.

    Rules that look for a construct in *code* must not fire on prose in a
    comment. Three of the rules here produced nothing but comment hits
    before this was added, so it is load-bearing rather than defensive.

    String literals are preserved, because several rules need to inspect them;
    a // inside a string literal is therefore correctly not treated as a
    comment start.
    """
    out = []
    i, n = 0, len(text)
    state = None  # None | 'line' | 'block' | '"' | "'"
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state is None:
            if ch == "/" and nxt == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if ch in ('"', "'"):
                state = ch
            out.append(ch)
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                state = None
                out.append(ch)
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                state = None
                out.append("  ")
                i += 2
                continue
            out.append(ch if ch == "\n" else " ")
            i += 1
            continue
        # inside a string literal
        if ch == "\\":
            out.append(text[i : i + 2])
            i += 2
            continue
        if ch == state:
            state = None
        out.append(ch)
        i += 1
    return "".join(out)


def strip_hash_comments(text: str) -> str:
    """Blank out # comments for shell/PKGBUILD/desktop-adjacent files."""
    lines = []
    for line in text.split("\n"):
        stripped = line.lstrip()
        if stripped.startswith("#"):
            lines.append("")
        else:
            lines.append(re.sub(r"(?<!\$)#(?![{(]).*$", "", line))
    return "\n".join(lines)


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


# --------------------------------------------------------------------------
# Rule: spdx
# --------------------------------------------------------------------------

# Data assets in formats with no comment syntax are exempt. CLAUDE.md names
# these exactly; adding a header to them makes the file invalid.
#
# FORWARD COVER, not a live guard, and the distinction is worth stating because the
# comment here used to imply otherwise. rule_spdx filters on CODE_SUFFIXES first, and
# that set holds no .json and no .in, so every path this pattern can match has already
# been skipped and the exemption cannot fire today. It stays because the day .json joins
# CODE_SUFFIXES — to check anything at all about a data file — the gate would start
# demanding a header on the files where one is invalid, and this is what stops it.
#
# .search(), not .match(): these are mid-path patterns, and .match() anchors
# at position 0, so the (^|/) alternation could never fire for a tier-
# prefixed path and the exemption would guard nothing even once it is reachable.
SPDX_EXEMPT = re.compile(r"(^|/)data/.*\.json$|(^|/)libs/phosphor-registry/tests/.*manifest\.json(\.in)?$")

# How far in a header may sit. Named because TWO rules read it and because the message
# quotes the number: widening it silently turns "in the first 6 lines" into a lie. FIVE is
# what the deepest header in the tree needs (packaging/arch/update-aur.sh: a shebang, two
# comment lines, then the two tags on 4 and 5), so six gives one line of slack. The band is
# closed INSIDE the selftest from both sides: the deep_ok probe puts its tags on 5 and 6 and
# must not be reported, which fails at 4 or 5, and the toodeep probe puts its tags past the
# window and must be reported, which fails at 7 or more. A whole-tree run independently
# rejects 4 and below (update-aur.sh). Only 6 survives all three.
SPDX_HEAD_LINES = 6


def rule_spdx(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        if Path(f).suffix not in CODE_SUFFIXES:
            continue
        if SPDX_EXEMPT.search(f):
            continue
        # Generated editor aids are gitignored and carry no header, but if one
        # is passed explicitly, honour the documented exemption.
        if Path(f).name == "p_generated.glsl":
            continue
        head = "\n".join(read(f).split("\n")[:SPDX_HEAD_LINES])
        if "SPDX-License-Identifier" not in head:
            out.append(Violation("spdx", f, 1,
                                 f"missing SPDX-License-Identifier in the first {SPDX_HEAD_LINES} lines"))
        elif "SPDX-FileCopyrightText" not in head:
            out.append(Violation("spdx", f, 1,
                                 f"missing SPDX-FileCopyrightText in the first {SPDX_HEAD_LINES} lines"))
    return out


# --------------------------------------------------------------------------
# Rule: license
# --------------------------------------------------------------------------

LGPL = "LGPL-2.1-or-later"
GPL3 = "GPL-3.0-or-later"

# The shader trees are deliberately mixed: a pack's .frag may be GPL (a port of
# GPL upstream, carrying a second copyright line crediting that author) while
# its .vert is PlasmaZones-original LGPL. The license follows the incorporated
# content, so it cannot be derived from the path. data/overlays and data/surface
# are likewise un-normalised. These trees are checked for header *presence*
# by the spdx rule and are exempt from the per-tree license rule.
# Anchored to the two real data trees. An unanchored "(^|/)data/" also
# swallowed plasmazones/tests/**/data/, quietly un-governing real GPL-3
# sources that happened to sit in a directory called data.
LICENSE_UNGOVERNED = re.compile(r"^(plasmazones|phosphor-libs)/data/")


def expected_license(path: str) -> str | None:
    if LICENSE_UNGOVERNED.search(path):
        return None
    if re.search(r"(^|/)libs/phosphor-", path):
        # A library's own tests follow the library. Test code that links and
        # ships inside an LGPL lib must not taint that lib's tree with GPL.
        return LGPL
    # The app tiers: plasmazones (daemon, editor, settings, KCM, KWin effect,
    # tools, tests) and phosphor-shell (binary, CLI, shell QML, tests), plus
    # the shell-libs example harnesses and the repo-level scripts.
    if path.startswith(("plasmazones/", "phosphor-shell/", "phosphor-shell-libs/examples/", "scripts/")):
        return GPL3
    return None


def rule_license(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        if Path(f).suffix not in CODE_SUFFIXES:
            continue
        want = expected_license(f)
        if want is None:
            continue
        head = "\n".join(read(f).split("\n")[:SPDX_HEAD_LINES])
        m = re.search(r"SPDX-License-Identifier:\s*(\S+)", head)
        if not m:
            # Normally the spdx rule reports this. Defer only when that rule is
            # actually running, or `--rules license` on its own reports a file
            # with no header whatsoever as clean.
            if "spdx" not in _SELECTED_RULES:
                out.append(Violation("license", f, 0, "no SPDX-License-Identifier"))
            continue  # otherwise reported by the spdx rule
        got = m.group(1)
        if got != want:
            out.append(
                Violation(
                    "license",
                    f,
                    line_of(head, m.start()),
                    f"license is {got}, but this tree is {want} "
                    f"({'reusable library, LGPL so third-party plugins can link it' if want == LGPL else 'app/daemon/editor tree'})",
                )
            )
    return out


# --------------------------------------------------------------------------
# Rule: file-size
# --------------------------------------------------------------------------


def load_baseline() -> dict[str, int]:
    if not BASELINE.exists():
        return {}
    return json.loads(BASELINE.read_text())["files"]


def rule_size(files: list[str]) -> list[Violation]:
    base = load_baseline()
    out = []
    for f in files:
        if Path(f).suffix not in CODE_SUFFIXES:
            continue
        n = len(read(f).splitlines())
        if n <= SIZE_CEILING:
            continue
        if f not in base:
            out.append(
                Violation(
                    "file-size",
                    f,
                    n,
                    f"new file is {n} lines, over the {SIZE_CEILING} hard ceiling; split it by concern",
                )
            )
        elif n > base[f]:
            out.append(
                Violation(
                    "file-size",
                    f,
                    n,
                    f"grandfathered file grew from {base[f]} to {n} lines; "
                    f"growing an existing overrun is a finding, shrink it or split it",
                )
            )
    return out


def update_baseline() -> int:
    files = tracked_files()
    # Read once. It was being re-read and re-parsed inside the loop, twice per unreadable
    # file, for a value that cannot change while the sweep runs.
    prior = load_baseline()
    rec = {}
    for f in files:
        if Path(f).suffix not in CODE_SUFFIXES:
            continue
        # An unreadable file must not be RECORDED. read() gives "", that is zero lines,
        # the file falls below the ceiling and its baseline entry silently disappears —
        # and the next run, once the mode is fixed, reports a grandfathered file as a
        # new one over the ceiling. Warn and keep going: dropping one entry from a
        # ratchet is worse than an incomplete sweep the operator can see.
        err = read_error(f)
        if err is not None:
            print(f"warning: {f}: cannot be read ({err}); leaving its baseline entry alone", file=sys.stderr)
            if f in prior:
                rec[f] = prior[f]
            continue
        n = len(read(f).splitlines())
        if n > SIZE_CEILING:
            rec[f] = n
    BASELINE.write_text(
        json.dumps(
            {
                "_comment": (
                    "Grandfathered files over the CLAUDE.md hard ceiling of "
                    f"{SIZE_CEILING} lines, recorded at their current length. "
                    "scripts/check-conventions.py fails if one of these grows or "
                    "if a new file appears over the ceiling. Shrinking a file "
                    "here is always welcome; re-run with --update-baseline to "
                    "ratchet the recorded length down. A file whose entry was "
                    "RAISED carries its own FILE-SIZE EXCEPTION comment saying "
                    "what it gained and why."
                ),
                "ceiling": SIZE_CEILING,
                "files": dict(sorted(rec.items())),
            },
            indent=2,
        )
        + "\n"
    )
    print(f"recorded {len(rec)} files over the {SIZE_CEILING}-line ceiling -> {BASELINE.relative_to(REPO)}")
    return 0


# --------------------------------------------------------------------------
# Rule: i18n-cpp
# --------------------------------------------------------------------------

# The i18n bridge itself necessarily names and wraps KLocalizedString; the rule
# targets ordinary call sites, not the implementation of the abstraction.
I18N_BRIDGE_ALLOW = {
    "plasmazones/src/phosphor_i18n.h",
    "plasmazones/src/phosphor_qml_i18n.h",
    "plasmazones/src/phosphor_qml_i18n.cpp",
    "phosphor-libs/libs/phosphor-control/include/PhosphorControl/LocalizedContext.h",
    "phosphor-libs/libs/phosphor-control/src/localizedcontext.cpp",
}

I18N_CALL = re.compile(r"(?<![\w:.])(i18n|i18nc|i18np|i18ncp)\s*\(")
KLOCALIZED_INCLUDE = re.compile(r"^\s*#\s*include\s*<KLocalizedString>", re.M)


def rule_i18n_cpp(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        if Path(f).suffix not in CPP_SUFFIXES:
            continue
        if f in I18N_BRIDGE_ALLOW or "/tests/" in f:
            continue
        code = strip_c_comments(read(f))
        for m in KLOCALIZED_INCLUDE.finditer(code):
            out.append(
                Violation(
                    "i18n-cpp",
                    f,
                    line_of(code, m.start()),
                    "C++ must not include <KLocalizedString>; use PhosphorI18n::tr()",
                )
            )
        for m in I18N_CALL.finditer(code):
            out.append(
                Violation(
                    "i18n-cpp",
                    f,
                    line_of(code, m.start()),
                    f"{m.group(1)}() is the QML API; C++ must use PhosphorI18n::tr()",
                )
            )
    return out


# --------------------------------------------------------------------------
# Rule: config-keys
# --------------------------------------------------------------------------

# v2 groups are dot-paths mirroring the UI hierarchy. A literal of that shape
# anywhere outside the accessor definitions means a config key was inlined
# instead of going through ConfigDefaults::.
CONFIG_DOTPATH = re.compile(
    r'QStringLiteral\(\s*"((?:Snapping|Tiling|Scrolling|General|Appearance|Editor|Shell|Rules|Profiles)\.[A-Za-z0-9.]+)"\s*\)'
)
CONFIG_KEY_DEFS = (
    "plasmazones/src/config/configkeys.h",
    "plasmazones/src/config/configdefaults.h",
    "plasmazones/src/config/configmigration.cpp",
)


def rule_config_keys(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        if Path(f).suffix not in CPP_SUFFIXES:
            continue
        if f in CONFIG_KEY_DEFS:
            continue
        # Tests that assert the on-disk group layout must pin the literal
        # path. Routing them through the accessor would make the assertion
        # tautological: it would then pass whatever the accessor returned,
        # including a wrong value. The rule targets production call sites.
        if "/tests/" in f:
            continue
        code = strip_c_comments(read(f))
        for m in CONFIG_DOTPATH.finditer(code):
            out.append(
                Violation(
                    "config-keys",
                    f,
                    line_of(code, m.start()),
                    f'inline config path "{m.group(1)}"; use the ConfigDefaults:: group/key accessors',
                )
            )
    return out


# --------------------------------------------------------------------------
# Rule: prose
# --------------------------------------------------------------------------

# The DETECTOR lives in conventions_prose.py; what stays here is the RULE that decides
# which files it reaches and how each surface's strings are extracted. This file hit the
# 1150-line hard ceiling, and CLAUDE.md says to split past it: three rounds running had
# to pay for a line-neutral rewrite just to correct a comment. The detector is the most
# separable concern left, because it is one pure function over a string that reads no
# file and imports nothing from here. Imported at module scope, unlike the three rule
# siblings, because it cannot import this file back, and selftest() passes it on.
_add_script_dir_to_path()
from conventions_prose import prose_problems  # noqa: E402  (needs the sys.path insert)

PROSE_STRING_KEYS = {
    "name",
    "description",
    "title",
    "summary",
    "comment",
    "genericname",
    "text",
    "highlight",
    "highlights",
    "label",
}


def iter_json_prose(path: str):
    # A parse failure is reported by the caller as a violation rather than
    # swallowed: returning quietly made a malformed data JSON indistinguishable
    # from one with no prose in it.
    try:
        doc = json.loads(read(path))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        yield None, str(exc)
        return

    def walk(node, trail):
        if isinstance(node, dict):
            for k, v in node.items():
                # A preset's KEY is its label: the shader pack schemas say the picker shows
                # the preset name verbatim, so it is user-facing prose even though no
                # "name" field holds it. Without this the only user-visible string in the
                # whole presets block goes unchecked.
                if k == "presets" and isinstance(v, dict):
                    for preset_name in v:
                        yield trail + "/presets", preset_name
                yield from walk(v, trail + "/" + k)
        elif isinstance(node, list):
            # Carry the LIST's key down to its elements. Walking with the index
            # as the trail segment meant a bare string inside an array was
            # keyed on a digit, which is never a prose key, so every
            # "highlights": [...] entry went unchecked.
            for i, v in enumerate(node):
                yield from walk(v, trail if isinstance(v, str) else f"{trail}/{i}")
        elif isinstance(node, str):
            # The last NON-INDEX segment. Belt and braces beside the list arm above,
            # which already hands a bare string element the ARRAY's trail: this also
            # covers a prose key reached through an index that the arm does not
            # flatten, and costs one generator expression to do it.
            key = next((seg for seg in reversed(trail.split("/")) if seg and not seg.isdigit()), "")
            if key.lower() in PROSE_STRING_KEYS:
                yield trail, node

    yield from walk(doc, "")


# Forms whose FIRST argument is the user-visible string.
TR_LITERAL = re.compile(
    r'(?<![\w:.])(?:PhosphorI18n::tr|qsTr|i18n|i18np)\s*\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)'
)
# Forms whose first argument is a disambiguation CONTEXT, never shown to a
# user. Matching them with the pattern above checked the context and left the
# real string unread, which is most of the i18nc call sites in the tree.
TR_CONTEXT_LITERAL = re.compile(
    r'(?<![\w:.])(?:i18nc|i18ncp|qsTranslate)\s*\(\s*'
    r'(?:"(?:[^"\\]|\\.)*"\s*)+,\s*'
    r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
)
# The 4th field of a PhosphorConfig::KeyDef is a human-readable description
# that consumers surface in settings UIs and generated documentation, so it is
# user-facing prose even though it is not routed through tr().
SCHEMA_DESCRIPTION = re.compile(
    r'QMetaType::\w+,\s*((?:QStringLiteral\(\s*"(?:[^"\\]|\\.)*"\s*\)\s*)+)'
)
DESKTOP_FIELD = re.compile(r"^(Name|GenericName|Comment)(\[[^\]]+\])?\s*=\s*(.+)$", re.M)
XML_PROSE = re.compile(r"<(summary|p|name|caption)(?:\s[^>]*)?>(.*?)</\1>", re.S)
PKG_DESC = re.compile(r"^\s*(?:pkgdesc|Summary|Description)\s*[=:]\s*(.+)$", re.M)
# The trailing pull-request reference on a changelog entry: markup, not prose.
CHANGELOG_REF = re.compile(r"\(\[#\d+\]\([^)]*\)(?:,\s*\[[^\]]*\]\([^)]*\))*\)")

# Nix meta. CLAUDE.md names it, but the pattern above never matched it: it is
# case-sensitive and Nix spells the attribute `description`. `longDescription`
# uses Nix's '' ... '' multi-line form, so it needs its own arm rather than a
# line match.
NIX_DESC = re.compile(r"^\s*description\s*=\s*\"((?:[^\"\\]|\\.)*)\"", re.M)
# Nix's '' ... '' form escapes a literal '' as ''' and an interpolation as
# ''${, so a non-greedy (.*?)'' stops at the ESCAPE rather than the
# terminator and the rest of the body goes unchecked. No regex is correct
# for that grammar, so the truncation is reported instead of passed over.
NIX_LONG_DESC = re.compile(r"^\s*longDescription\s*=\s*''(.*?)''", re.M | re.S)
# (?!'') rather than .*? : a lazy any-scan runs to the END OF FILE, so a
# clean longDescription followed anywhere later by an ordinary ''${...}
# wrapper hook fired this rule spuriously. Stopping at the FIRST '' means
# the lookahead only ever inspects this body's own terminator.
#
# The class is [$'\\] because Nix has THREE escapes that begin with '', not
# two: ''${ for a literal interpolation, ''' for a literal '', and ''\<char>
# for a character escape (''\n, ''\t, ''\', ''\\). Omitting the backslash let
# a body whose first escape was ''\n read as terminated there, so everything
# after it went unchecked.
NIX_LONG_DESC_ESCAPE = re.compile(r"^\s*longDescription\s*=\s*''(?:(?!'').)*''(?=[$'\\])", re.M | re.S)

# RPM's %description body runs from the directive to the next % section. The
# PKG_DESC pattern cannot see it, so `dnf info` printed sixteen ungated lines.
# `|\Z` so a %description that ENDS the file still matches. Without it the
# lookahead simply fails and the whole block is skipped, which would let a
# subpackage description appended at EOF go ungated.
RPM_DESC = re.compile(r"^%description[^\n]*\n(.*?)(?=^%\w|\Z)", re.M | re.S)

# deb822's Description field: a one-line synopsis then a continuation block, every line
# of which begins with a single space. PKG_DESC catches only the synopsis, so the body
# `apt show` prints was ungated — the odd one out, since the RPM and Nix bodies each got
# their own arm. `^\S` ends it, which is the next field or the blank line between
# paragraphs, so the pattern stops at the field rather than running into the next one.
DEB_DESC = re.compile(r"^Description:[^\n]*\n((?:[ \t]+[^\n]*\n)+)", re.M)

# deb822 continuation-line markers, stripped before the prose test. A "  - " bullet is a
# LIST MARKER in this format, not a spaced hyphen standing in for a dash, and leaving it
# in reported every bullet in the file.
DEB_DESC_BULLET = re.compile(r"^[ \t]*[-*]\s+", re.M)


def rule_prose(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        suffix = Path(f).suffix

        if re.search(r"(^|/)data/", f) and suffix == ".json":
            for trail, s in iter_json_prose(f):
                if trail is None:
                    out.append(Violation("prose", f, 0, f"malformed JSON: {s}"))
                    continue
                for p in prose_problems(s):
                    out.append(Violation("prose", f, 0, f"{trail}: {p} -> {s[:80]!r}"))
            continue

        if suffix in CPP_SUFFIXES | QML_SUFFIXES:
            code = strip_c_comments(read(f))
            for pattern in (TR_LITERAL, TR_CONTEXT_LITERAL):
                for m in pattern.finditer(code):
                    lit = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
                    for p in prose_problems(lit):
                        out.append(Violation("prose", f, line_of(code, m.start()), f"{p} -> {lit[:80]!r}"))
            if Path(f).name.startswith("settingsschema"):
                for m in SCHEMA_DESCRIPTION.finditer(code):
                    lit = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
                    for p in prose_problems(lit):
                        out.append(
                            Violation("prose", f, line_of(code, m.start()), f"setting description: {p} -> {lit[:80]!r}")
                        )
            continue

        if ".desktop" in f:
            for m in DESKTOP_FIELD.finditer(read(f)):
                for p in prose_problems(m.group(3)):
                    out.append(Violation("prose", f, 0, f"{m.group(1)}: {p} -> {m.group(3)[:80]!r}"))
            continue

        if "metainfo" in f and suffix == ".xml":
            text = read(f)
            for m in XML_PROSE.finditer(text):
                body = re.sub(r"\s+", " ", m.group(2)).strip()
                for p in prose_problems(body):
                    out.append(Violation("prose", f, line_of(text, m.start()), f"{p} -> {body[:80]!r}"))
            continue

        # CLAUDE.md names "CHANGELOG.md entries" among the user-facing surfaces
        # these rules govern, and nothing here checked them, so every release
        # note this project has ever written went through the gate unread.
        #
        # Only the ENTRY BODY is checked. The Keep-a-Changelog "**Term**:"
        # lead-in is an explicitly allowed colon, headings are structure rather
        # than prose, and the trailing ([#nnnn](url)) reference is markup whose
        # URL would otherwise read as prose punctuation.
        #
        # FOUR THINGS THIS ARM DOES NOT CATCH, so a reviewer still has to read:
        #   1. the entry's bold TITLE, which the split below discards;
        #   2. the dramatic "Label: payload" colon;
        #   3. the rule-of-three triad and "not just X, but Y" (prose_problems
        #      tests neither 2 nor 3);
        #   4. a clause-splicing semicolon from past-tense prose, because the verb
        #      list the semicolon arm matches holds no past-tense lexical verbs
        #      ("The pane is one shape; each pack decided it alone." passes).
        # Sub-bullets ARE checked: the lstrip below reaches an indented "- " too.
        if Path(f).name == "CHANGELOG.md":
            text = read(f)
            for n, ln in enumerate(text.splitlines(), 1):
                ln = ln.lstrip()
                if not ln.startswith("- "):
                    continue
                body = ln.split("**:", 1)[1] if "**:" in ln else ln[2:]
                body = CHANGELOG_REF.sub("", body)
                for pr in prose_problems(body):
                    out.append(Violation("prose", f, n, f"{pr} -> {body.strip()[:80]!r}"))
            continue

        # .github/workflows too: the draft PKGBUILD pkgdesc is generated in
        # ci.yml and release.yml, pacman prints it, and this arm used to stop
        # at the packaging/ prefix so those strings were invisible.
        if f.startswith("packaging/") or (f.startswith(".github/workflows/") and suffix in (".yml", ".yaml")):
            body = read(f)
            stripped = strip_hash_comments(body)
            for m in PKG_DESC.finditer(stripped):
                for p in prose_problems(m.group(1)):
                    out.append(Violation("prose", f, 0, f"{p} -> {m.group(1)[:80]!r}"))
            # Nix and RPM bodies the line-oriented pattern above cannot reach.
            # Read from the raw text, not the hash-stripped copy: `#` is not a
            # comment inside a Nix string or an RPM %description.
            if suffix == ".nix":
                for m in NIX_LONG_DESC_ESCAPE.finditer(body):
                    out.append(Violation("prose", f, line_of(body, m.start()),
                                         "longDescription contains a Nix '' escape; this rule cannot "
                                         "read past it, so the rest of the body is unchecked"))
                for pat in (NIX_DESC, NIX_LONG_DESC):
                    for m in pat.finditer(body):
                        for p in prose_problems(m.group(1)):
                            out.append(Violation("prose", f, line_of(body, m.start()),
                                                 f"{p} -> {m.group(1).strip()[:80]!r}"))
            if suffix == ".spec":
                for m in RPM_DESC.finditer(body):
                    # Same bullet strip as the deb822 body below. An RPM %description is
                    # free-form, so its feature list is indistinguishable from prose to
                    # this rule: the live one passes only because its `-` markers sit at
                    # column 0, and an ordinary reflow that indented them turned every
                    # bullet into "spaced hyphen used as a dash" and blocked the commit,
                    # while the identical indented list in debian/control reported nothing.
                    para = DEB_DESC_BULLET.sub("", m.group(1))
                    for p in prose_problems(para):
                        out.append(Violation("prose", f, line_of(body, m.start()),
                                             f"{p} -> {para.strip()[:80]!r}"))
            # debian/control has no suffix, so it is matched by NAME. CLAUDE.md lists the
            # "Debian Description" among the surfaces these rules govern.
            if Path(f).name == "control":
                for m in DEB_DESC.finditer(body):
                    para = DEB_DESC_BULLET.sub("", m.group(1))
                    for p in prose_problems(para):
                        out.append(Violation("prose", f, line_of(body, m.start()),
                                             f"{p} -> {para.strip()[:80]!r}"))
            continue

        if f.startswith("plasmazones/data/algorithms/") and suffix == ".luau":
            code = strip_c_comments(read(f))
            for m in re.finditer(r'description\s*=\s*"((?:[^"\\]|\\.)*)"', code):
                for p in prose_problems(m.group(1)):
                    out.append(Violation("prose", f, line_of(code, m.start()), f"{p} -> {m.group(1)[:80]!r}"))
            continue

    return out


# --------------------------------------------------------------------------
# Rule: dep5
# --------------------------------------------------------------------------
#
# The data and the check live in conventions_dep5.py. This file reached the
# 1150-line ceiling and CLAUDE.md says to split past it; dep5 is the most
# separable rule, and the sibling takes the repo root and the two helpers it needs
# as arguments so the two cannot form an import cycle. It reads file heads itself,
# in binary and bounded, because it is the one rule that meets binaries and
# symlinks, so it does not take read().
def rule_dep5(files: list[str]) -> list[Violation]:
    _add_script_dir_to_path()
    from conventions_dep5 import dep5_problems

    return [Violation("dep5", p, ln, m) for p, ln, m in
            dep5_problems(files, repo=REPO, line_of=line_of,
                          tracked_files=tracked_files)]


# --------------------------------------------------------------------------
# Rule: js-pragma
# --------------------------------------------------------------------------

# qt_add_qml_module writes a qmldir entry for every .js whose basename starts
# uppercase, and for those it checks that the file declares itself a shared
# library:
#
#     file(STRINGS ${qml_file_src} pragma_library
#          REGEX "^\.pragma library$" LIMIT_COUNT 1 LIMIT_INPUT 128)
#
# (Qt6QmlMacros.cmake). Only the first 128 BYTES are searched. Push
# `.pragma library` past that and Qt emits an AUTHOR_WARNING saying the file
# will be re-evaluated in every importing document. The file still behaves as
# a library, so the warning is false, but it is indistinguishable from a real
# one and there is no way to silence it short of moving the line back.
#
# This is worth a gate rather than a comment because the margin is thin and
# shared: two SPDX lines plus a blank put the pragma at byte ~105 in most of
# these files, leaving around twenty bytes. One more header line, or a longer
# licence identifier applied tree-wide, would trip every one of them at once
# and the failure would arrive as a wall of warnings from files nobody edited.
JS_PRAGMA_WINDOW = 128
JS_PRAGMA = b".pragma library"


def rule_js_pragma(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        name = Path(f).name
        # Mirrors Qt's own guards, which are narrower than they look:
        #   - lowercase basenames get no qmldir entry, so Qt never checks them;
        #   - the gate is `qml_file_ext STREQUAL ".js"`, and CMake's EXT for
        #     Foo.bar.js is ".bar.js", so a multi-dot name is skipped there
        #     while Path.suffix would still say ".js";
        #   - Qt's MATCHES "^[A-Z]" is ASCII, while str.isupper() is Unicode,
        #     so "Ärger.js" would be flagged here and skipped by Qt.
        # Neither multi-dot nor non-ASCII-initial .js exists in the tree today;
        # matching Qt exactly keeps it that way if one ever lands.
        #
        # NOT modelled: QT_QML_SKIP_QMLDIR_ENTRY. Qt runs the pragma check only
        # when that source-file property is unset, and plasmazones/src/
        # CMakeLists.txt sets it TRUE on editor/qml/ColorUtils.js, which Qt
        # therefore never inspects. Parsing CMake to learn that is not worth it
        # for one file, so the rule is stricter than Qt there by design.
        if name.count(".") != 1 or not name.endswith(".js"):
            continue
        if name[:1] not in string.ascii_uppercase:
            continue
        try:
            whole = (REPO / f).read_bytes()
        except OSError:
            continue
        # CMake's file(STRINGS) strips a UTF-8 BOM before matching, so a BOM'd
        # file satisfies Qt. Strip it here too, and offset the byte figures
        # back so they still describe the real file.
        bom = len(codecs.BOM_UTF8) if whole.startswith(codecs.BOM_UTF8) else 0
        body = whole[bom:]
        # Anchored per line, like Qt's regex: a `.pragma library` sitting
        # inside a comment or trailing another statement is not what CMake
        # matches, so it must not satisfy this rule either.
        # The window is measured from byte 0 of the FILE, and LIMIT_INPUT counts
        # the BOM against it, so the usable budget shrinks by the BOM's length.
        # Slicing body[:128] would hand back the three bytes the BOM already
        # spent and pass a file CMake rejects.
        head = body[: JS_PRAGMA_WINDOW - bom]
        if any(ln.strip(b"\r") == JS_PRAGMA for ln in head.split(b"\n")):
            continue
        idx = body.find(JS_PRAGMA)
        if idx < 0:
            out.append(Violation("js-pragma", f, 0,
                                 "no '.pragma library'; Qt will warn that this file is re-evaluated "
                                 "per importing document (rename it lowercase if that is intended)"))
            continue
        # Count newlines in BYTES. line_of() counts characters, so handing it a
        # byte offset misreports the line for any file with multibyte UTF-8
        # above the pragma.
        line = body.count(b"\n", 0, idx) + 1
        end = bom + idx + len(JS_PRAGMA)
        if end > JS_PRAGMA_WINDOW:
            out.append(Violation("js-pragma", f, line,
                                 f"'.pragma library' ends at byte {end}, past Qt's "
                                 f"{JS_PRAGMA_WINDOW}-byte window; move it above the description"))
        else:
            # Inside the window, so the only way the line scan missed it is
            # that it is not alone on its own line. Saying "past the window"
            # here would send the reader to move a line already in the right
            # place.
            out.append(Violation("js-pragma", f, line,
                                 "'.pragma library' is present but not alone on its own line; "
                                 "Qt matches the anchored regex ^\\.pragma library$"))
    return out


# Rule: shared-param-text — a control the host resolves once per chain must carry the
# same DESCRIPTION on every pack offering it. Description only. `name`, `type` and
# `default` are single-valued across the twenty today and would drift silently; `group`
# legitimately varies (8 packs say "Shape", 12 omit it), because it is presentation and
# the 12 group nothing at all, so a flat list is what they render. The data and the
# check live in conventions_shared_text.py, for the reason the self-test arm below records.
def rule_shared_param_text(files: list[str]) -> list[Violation]:
    # `files` is unused: whether the twenty agree is not answerable from a staged
    # subset, and the failure to catch is a commit that updates nineteen and leaves the
    # twentieth, whose file is then the one NOT staged. So this always globs and can
    # name a file the commit did not touch, which is the honest answer.
    del files
    _add_script_dir_to_path()
    from conventions_shared_text import shared_param_problems

    return [Violation("shared-param-text", p, 0, m) for p, m in shared_param_problems(REPO)]


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------

RULES = {
    "spdx": (rule_spdx, "SPDX header present on every file whose format supports comments"),
    "license": (rule_license, "GPL-3 app tree vs LGPL-2.1 libs/phosphor-* split"),
    "file-size": (rule_size, f"no new file over {SIZE_CEILING} lines, no growth of a grandfathered one"),
    "i18n-cpp": (rule_i18n_cpp, "C++ uses PhosphorI18n::tr(), never i18n()/KLocalizedString"),
    "config-keys": (rule_config_keys, "config group/key strings go through ConfigDefaults:: accessors"),
    "prose": (rule_prose, "user-facing strings carry no em-dash splice, clause semicolon or spaced hyphen"),
    "dep5": (rule_dep5, "packaging/debian/copyright declares each file's real license and holders"),
    "js-pragma": (rule_js_pragma, f"QML .js libraries declare '.pragma library' in Qt's first {JS_PRAGMA_WINDOW} bytes"),
    "shared-param-text": (rule_shared_param_text, "a chain-resolved param's description matches on every pack"),
}


# --------------------------------------------------------------------------
# Self-test
# --------------------------------------------------------------------------
#
# The data and the checks live in conventions_selftest.py. This file crossed the
# 1150-line ceiling when two branches each added a rule, and the self-test is the
# one section that depends on nothing but what it is handed, so it is what
# moved. It imports NOTHING from this file — the two detectors and the readability
# precondition are passed in as arguments, which is what keeps the pair acyclic.
# Still imported inside the function rather than at module scope, so a run that
# never asks for the self-test does not pay for parsing it.


def selftest() -> int:
    # The sibling is found by THIS FILE's directory, not by sys.path[0]. Those
    # coincide for `python3 scripts/check-conventions.py`, which is how lefthook and
    # CI invoke it, and diverge for anything that runs a copy from elsewhere — where
    # the failure would be an ImportError that reads like a selftest failure.
    _add_script_dir_to_path()
    from conventions_selftest import run_selftest

    return run_selftest(prose_problems, iter_json_prose, partition_readable)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="limit the check to these paths (default: the whole tree)")
    ap.add_argument("--rules", help="comma-separated subset of rules to run")
    ap.add_argument(
        "--staged",
        action="store_true",
        help="treat an empty FILES list as nothing to check rather than as "
             "the whole tree. For pre-commit hooks, where the glob can "
             "filter every staged path away and a bare invocation would "
             "otherwise silently become a whole-tree run.",
    )
    ap.add_argument("--list-rules", action="store_true")
    ap.add_argument("--selftest", action="store_true", help="check the rules can still see what they are meant to")
    ap.add_argument("--update-baseline", action="store_true", help="re-record the oversize-file baseline")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if args.list_rules:
        for name, (_, desc) in RULES.items():
            print(f"{name:18} {desc}")
        # Listed because it prints findings under a rule name, and a developer who
        # meets one looks here first. Marked always-on because --rules cannot
        # select or deselect it: it is a precondition, not a convention.
        print(f"{'unreadable':18} (always on) every path handed to a rule can be read as text")
        return 0

    if args.update_baseline:
        return update_baseline()

    selected = list(RULES)
    if args.rules:
        # Deduped in order: `--rules a,a` ran the rule twice and double-printed it.
        selected = list(dict.fromkeys(r.strip() for r in args.rules.split(",")))
        unknown = [r for r in selected if r not in RULES]
        if unknown:
            print(f"unknown rule(s): {', '.join(unknown)}", file=sys.stderr)
            return 2

    global _SELECTED_RULES
    _SELECTED_RULES = set(selected)

    if args.staged and not args.files:
        # Nothing staged matched the hook's globs. Not an error, and not a
        # reason to sweep the tree.
        return 0

    if args.files:
        files = []
        for f in args.files:
            p = Path(f).resolve()
            try:
                rel = str(p.relative_to(REPO))
            except ValueError:
                print(f"check-conventions: skipping {f} (outside the repository)", file=sys.stderr)
                continue
            if rel.startswith(EXCLUDED_PREFIXES):
                continue
            if not p.is_file():
                # Announced, not dropped in silence: a stale or hand-passed list would check NOTHING.
                # A hand-run or another runner reaches it; lefthook drops a non-file staged path
                # (verified against lefthook 2.1.14) and its globs match no tracked symlink anyway.
                print(f"check-conventions: skipping {f} (not a file)", file=sys.stderr)
                continue
            files.append(rel)
    else:
        files = tracked_files()

    files, violations = partition_readable(files)

    for name in selected:
        violations.extend(RULES[name][0](files))

    if not violations:
        return 0

    violations.sort(key=lambda v: (v.rule, v.path, v.line))
    for v in violations:
        print(v.format())
    by_rule: dict[str, int] = {}
    for v in violations:
        by_rule[v.rule] = by_rule.get(v.rule, 0) + 1
    print(
        f"\n{len(violations)} convention violation(s): "
        + ", ".join(f"{k} x{n}" for k, n in sorted(by_rule.items())),
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
