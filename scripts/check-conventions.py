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
import json
import os
import re
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
CODE_SUFFIXES = CPP_SUFFIXES | QML_SUFFIXES | SHADER_SUFFIXES | {".luau", ".py"}

# Trees that are vendored or generated and are not ours to police.
EXCLUDED_PREFIXES = ("extern/", "build/", "build-noshell/", "build-nounity/")


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
SPDX_EXEMPT = re.compile(r"^data/.*\.json$|^libs/phosphor-registry/tests/.*manifest\.json(\.in)?$")


def rule_spdx(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        if Path(f).suffix not in CODE_SUFFIXES:
            continue
        if SPDX_EXEMPT.match(f):
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
LICENSE_UNGOVERNED = ("data/",)


def expected_license(path: str) -> str | None:
    if path.startswith(LICENSE_UNGOVERNED):
        return None
    if path.startswith("libs/phosphor-"):
        # A library's own tests follow the library. Test code that links and
        # ships inside an LGPL lib must not taint that lib's tree with GPL.
        return LGPL
    if path.startswith(("src/", "kcm/", "kwin-effect/", "examples/", "tests/", "tools/", "scripts/", "cli/")):
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
            continue  # reported by the spdx rule
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
        n = read(f).count("\n") + 1
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
        n = read(f).count("\n") + 1
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
                    "ratchet the recorded length down."
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
    "src/phosphor_i18n.h",
    "src/phosphor_qml_i18n.h",
    "src/phosphor_qml_i18n.cpp",
    "libs/phosphor-control/include/PhosphorControl/LocalizedContext.h",
    "libs/phosphor-control/src/localizedcontext.cpp",
}

I18N_CALL = re.compile(r"(?<![\w:.])(i18n|i18nc|i18np|i18ncp)\s*\(")
KLOCALIZED_INCLUDE = re.compile(r"^\s*#\s*include\s*<KLocalizedString>", re.M)


def rule_i18n_cpp(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        if Path(f).suffix not in CPP_SUFFIXES:
            continue
        if f in I18N_BRIDGE_ALLOW or f.startswith("tests/") or "/tests/" in f:
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
CONFIG_KEY_DEFS = ("src/config/configkeys.h", "src/config/configdefaults.h", "src/config/configmigration.cpp")


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
        if f.startswith("tests/") or "/tests/" in f:
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

PROSE_STRING_KEYS = {"name", "description", "title", "summary", "comment", "genericname", "text", "highlight", "label"}


def prose_problems(s: str) -> list[str]:
    problems = []
    # A "#"-led line inside a translatable string is a shell comment in
    # pasteable terminal text, not prose. CLAUDE.md puts code comments out of
    # scope, and that does not stop being true because the snippet is rendered
    # in a label.
    s = "\n".join(ln for ln in s.split("\n") if not ln.lstrip().startswith("#"))
    core = s.strip()
    if "—" in s or "&mdash;" in s:
        if not is_title_separator(core):
            problems.append("em-dash splice; write two sentences or join with a plain word")
    if " - " in s:
        problems.append("spaced hyphen used as a dash; rewrite the sentence")
    # Clause-splicing semicolon: only when both sides look like independent
    # clauses. Semicolons inside backticked code and those separating genuine
    # comma-bearing list items are legitimate, so both are excluded.
    without_code = re.sub(r"`[^`]*`", "", s)
    for part in re.finditer(r";\s+(\w+)", without_code):
        before = without_code[: part.start()]
        after = without_code[part.start() + 1 :]
        if "," in before or "," in after:
            continue  # list separator, not a clause splice
        if len(before.split()) >= 3 and len(after.split()) >= 3:
            problems.append("clause-splicing semicolon; split into sentences or use \"and\"")
            break
    return problems


def iter_json_prose(path: str):
    try:
        doc = json.loads(read(path))
    except (json.JSONDecodeError, UnicodeDecodeError):
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
            for i, v in enumerate(node):
                yield from walk(v, f"{trail}/{i}")
        elif isinstance(node, str):
            key = trail.rsplit("/", 1)[-1]
            if key.lower() in PROSE_STRING_KEYS:
                yield trail, node

    yield from walk(doc, "")


TR_LITERAL = re.compile(
    r'(?<![\w:.])(?:PhosphorI18n::tr|qsTr|i18n|i18nc|i18np|i18ncp)\s*\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)'
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


def rule_prose(files: list[str]) -> list[Violation]:
    out = []
    for f in files:
        suffix = Path(f).suffix

        if f.startswith("data/") and suffix == ".json":
            for trail, s in iter_json_prose(f):
                for p in prose_problems(s):
                    out.append(Violation("prose", f, 0, f"{trail}: {p} -> {s[:80]!r}"))
            continue

        if suffix in CPP_SUFFIXES | QML_SUFFIXES:
            code = strip_c_comments(read(f))
            for m in TR_LITERAL.finditer(code):
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

        if f.startswith("packaging/"):
            for m in PKG_DESC.finditer(strip_hash_comments(read(f))):
                for p in prose_problems(m.group(1)):
                    out.append(Violation("prose", f, 0, f"{p} -> {m.group(1)[:80]!r}"))
            continue

        if f.startswith("data/algorithms/") and suffix == ".luau":
            code = strip_c_comments(read(f))
            for m in re.finditer(r'description\s*=\s*"((?:[^"\\]|\\.)*)"', code):
                for p in prose_problems(m.group(1)):
                    out.append(Violation("prose", f, line_of(code, m.start()), f"{p} -> {m.group(1)[:80]!r}"))
            continue

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
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="limit the check to these paths (default: the whole tree)")
    ap.add_argument("--rules", help="comma-separated subset of rules to run")
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
