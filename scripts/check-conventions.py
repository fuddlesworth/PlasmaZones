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

CPP_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp"}
QML_SUFFIXES = {".qml"}
SHADER_SUFFIXES = {".frag", ".vert", ".glsl"}
# .js is here so rule_spdx and rule_license cover the 17 QML .js libraries.
# rule_js_pragma already polices them; the licence split on that file class
# was otherwise unenforced.
CODE_SUFFIXES = CPP_SUFFIXES | QML_SUFFIXES | SHADER_SUFFIXES | {".luau", ".py", ".js"}

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
    return [f for f in out if f and not f.startswith(EXCLUDED_PREFIXES)]


def read(path: str) -> str:
    return (REPO / path).read_text(encoding="utf-8", errors="replace")


def strip_c_comments(text: str) -> str:
    """Blank out // and /* */ comments, preserving line structure and offsets.

    Rules that look for a construct in *code* must not fire on prose in a
    comment. Three of the six rules here produced nothing but comment hits
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
# .search(), not .match(): these are mid-path patterns, and .match() anchors
# at position 0, so the (^|/) alternation could never fire for a tier-
# prefixed path and the exemption guarded nothing.
SPDX_EXEMPT = re.compile(r"(^|/)data/.*\.json$|(^|/)libs/phosphor-registry/tests/.*manifest\.json(\.in)?$")


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
        head = "\n".join(read(f).split("\n")[:6])
        if "SPDX-License-Identifier" not in head:
            out.append(Violation("spdx", f, 1, "missing SPDX-License-Identifier in the first 6 lines"))
        elif "SPDX-FileCopyrightText" not in head:
            out.append(Violation("spdx", f, 1, "missing SPDX-FileCopyrightText in the first 6 lines"))
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
LICENSE_UNGOVERNED = re.compile(r"^(plasmazones|phosphor)/data/")


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
        head = "\n".join(read(f).split("\n")[:6])
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
    rec = {}
    for f in files:
        if Path(f).suffix not in CODE_SUFFIXES:
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

# A literal typographic separator between two nouns is explicitly allowed (the
# "%1 — %2" Layout/Zone display format), and so are settings-path breadcrumbs.
SENTENCE_END = re.compile(r"[.!?](?:\s|$)")


def is_title_separator(s: str) -> bool:
    """True for the allowed "<noun phrase> — <noun phrase>" display format.

    CLAUDE.md permits a literal typographic separator between two nouns, the
    canonical case being the "%1 — %2" Layout/Zone format. The test is that
    the string is a label rather than prose: exactly one em-dash, no sentence
    punctuation on either side, and a short noun phrase each side.
    """
    parts = s.split("—")
    if len(parts) != 2:
        return False
    for side in parts:
        side = side.strip()
        if not side or SENTENCE_END.search(side):
            return False
        if len(side.split()) > 5:
            return False
    return True

PROSE_STRING_KEYS = {"name", "description", "title", "summary", "comment", "genericname", "text",
                     "highlight", "highlights", "label"}


def prose_problems(s: str) -> list[str]:
    problems = []
    # A "#"-led line inside a translatable string is a shell comment in
    # pasteable terminal text, not prose. CLAUDE.md puts code comments out of
    # scope, and that does not stop being true because the snippet is rendered
    # in a label.
    s = "\n".join(ln for ln in s.split("\n") if not ln.lstrip().startswith("#"))
    # Backticked code is out of scope for ALL THREE punctuation arms, not just
    # the semicolon one: CLAUDE.md puts code out of scope generally, and a
    # `--flag - value` or an em-dash inside a quoted command is code the reader
    # must see verbatim. Strip once, up front, and test every arm against the
    # stripped copy.
    without_code = re.sub(r"`[^`]*`", "", s)
    core = without_code.strip()
    if "—" in without_code or "&mdash;" in without_code:
        if not is_title_separator(core):
            problems.append("em-dash splice; write two sentences or join with a plain word")
    if " - " in without_code:
        problems.append("spaced hyphen used as a dash; rewrite the sentence")
    # Clause-splicing semicolon: only when both sides look like independent
    # clauses. Semicolons separating genuine comma-bearing list items are
    # legitimate, so those are excluded too.
    # Segment first, then look for the splice inside a segment. The comma
    # exclusion below is about the clause pair around THIS semicolon; applied
    # to the whole string it meant that one comma anywhere in a multi-paragraph
    # block (an RPM %description, a Nix longDescription, any CHANGELOG entry)
    # switched the rule off for every sentence in it.
    for segment in re.split(r"(?<=[.!?])\s+|\n\s*\n", without_code):
        for part in re.finditer(r";\s+(\w+)", segment):
            before = segment[: part.start()]
            after = segment[part.start() + 1 :]
            if "," in before or "," in after:
                continue  # list separator, not a clause splice
            if len(before.split()) >= 3 and len(after.split()) >= 3:
                problems.append("clause-splicing semicolon; split into sentences or use \"and\"")
                return problems
    return problems


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
            key = trail.rsplit("/", 1)[-1]
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
                    for p in prose_problems(m.group(1)):
                        out.append(Violation("prose", f, line_of(body, m.start()),
                                             f"{p} -> {m.group(1).strip()[:80]!r}"))
            continue

        if f.startswith("plasmazones/data/algorithms/") and suffix == ".luau":
            code = strip_c_comments(read(f))
            for m in re.finditer(r'description\s*=\s*"((?:[^"\\]|\\.)*)"', code):
                for p in prose_problems(m.group(1)):
                    out.append(Violation("prose", f, line_of(code, m.start()), f"{p} -> {m.group(1)[:80]!r}"))
            continue

    return out


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
    "js-pragma": (rule_js_pragma, f"QML .js libraries declare '.pragma library' in Qt's first {JS_PRAGMA_WINDOW} bytes"),
}


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
    ap.add_argument("--update-baseline", action="store_true", help="re-record the oversize-file baseline")
    args = ap.parse_args()

    if args.list_rules:
        for name, (_, desc) in RULES.items():
            print(f"{name:14} {desc}")
        return 0

    if args.update_baseline:
        return update_baseline()

    selected = list(RULES)
    if args.rules:
        selected = [r.strip() for r in args.rules.split(",")]
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
                continue
            if p.is_file() and not rel.startswith(EXCLUDED_PREFIXES):
                files.append(rel)
    else:
        files = tracked_files()

    violations: list[Violation] = []
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
