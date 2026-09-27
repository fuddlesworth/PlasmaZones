# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""The DEP-5 copyright rule for check-conventions.py.

Split out of the gate for the reason its two siblings record: the gate reached its
1150-line ceiling, and CLAUDE.md says to split past it rather than shave comments.
This is the most separable rule — self-contained, the only one that walks every
tracked path rather than a suffix-filtered subset, and dependent on nothing but the
repo root and two gate helpers, which arrive as keyword arguments the way
run_selftest's do.

Returns plain (path, line, message) tuples rather than the gate's Violation, so the
two files cannot form an import cycle.
"""
from __future__ import annotations

import fnmatch
import re


# The Debian DEP-5 file tells every downstream redistributor what each shipped
# file's license is. Nothing kept it honest, so it drifted: it declared 165
# LGPL shader-pack files as GPL-3, which defeats the whole reason those trees
# are LGPL. Reconciling it once fixes today and nothing else, because the next
# pack added under data/ breaks it again silently. This rule is the ratchet.
#
# It reads only the file HEAD, like the license rule, so SPDX text appearing in
# a string literal or in a contributing guide's example is not mistaken for a
# header.
DEP5_HEAD_LINES = 8


def dep5_head(rel: str, repo) -> str | None:
    """The first DEP5_HEAD_LINES lines, or None for anything without a readable
    text head. This rule is the one that walks EVERY tracked path rather than a
    suffix-filtered subset, so it meets what the others never see: binary assets.

    It does NOT meet the symlinked skill directories under .agents/, which an
    earlier version of this docstring claimed. tracked_files() filters on
    is_file(), and all five tracked symlinks point at directories, so they never
    reach this function by either route — whole-tree or the widened one, which
    also goes through tracked_files(). The is_file() guard below is therefore
    defensive on every production path. Reading a bounded slice keeps a
    whole-tree run cheap."""
    p = repo / rel
    try:
        if not p.is_file():
            return None
        with p.open("rb") as fh:
            raw = fh.read(4096)
    except OSError:
        return None
    if b"\0" in raw:
        return None
    return "\n".join(raw.decode("utf-8", errors="replace").split("\n")[:DEP5_HEAD_LINES])


def parse_dep5(dep5) -> list[dict]:
    """The Files stanzas in declaration order. DEP-5 resolution is last-match-wins."""
    stanzas: list[dict] = []
    cur: dict | None = None
    field: str | None = None
    for raw in dep5.read_text(encoding="utf-8").split("\n"):
        line = raw.rstrip()
        if line.startswith("#"):
            continue
        if not line.strip():
            if cur and cur["files"]:
                stanzas.append(cur)
            cur, field = None, None
            continue
        m = re.match(r"^(\S+):\s*(.*)$", line)
        if m:
            key, val = m.group(1).lower(), m.group(2).strip()
            if key == "files":
                cur = {"files": [val] if val else [], "copyright": [], "license": ""}
                field = "files"
            elif cur is not None and key == "copyright":
                if val:
                    cur["copyright"].append(val)
                field = "copyright"
            elif cur is not None and key == "license":
                cur["license"] = val
                field = "license"
            else:
                field = None
        elif line.startswith((" ", "\t")) and cur is not None and field in ("files", "copyright"):
            cur[field].append(line.strip())
    if cur and cur["files"]:
        stanzas.append(cur)
    return stanzas


def dep5_stanza_for(rel: str, stanzas: list[dict]) -> tuple[dict, str] | tuple[None, None]:
    """Last matching stanza wins. DEP-5 globs: * spans any run of characters,
    including '/', which is why fnmatch is right here and Path.match is not.

    Returns the pattern that matched alongside the stanza, not just the stanza's
    first pattern: a stanza that lists eight paths would otherwise point the
    reader at the wrong one."""
    hit: tuple[dict, str] | tuple[None, None] = (None, None)
    for s in stanzas:
        for pat in s["files"]:
            if fnmatch.fnmatchcase(rel, pat):
                hit = (s, pat)
                break
    return hit


def dep5_problems(files, *, repo, line_of, tracked_files):
    dep5 = repo / "packaging" / "debian" / "copyright"
    if not dep5.exists():
        return []
    stanzas = parse_dep5(dep5)
    if not stanzas:
        return [(str(dep5.relative_to(repo)), 0, "no Files stanza parsed")]

    # Editing the DEP-5 file can break any file in the tree, not only the ones
    # staged beside it, so that edit widens the check to everything.
    targets = tracked_files() if str(dep5.relative_to(repo)) in files else files

    out = []
    for f in targets:
        head = dep5_head(f, repo)
        if head is None:
            continue
        m = re.search(r"SPDX-License-Identifier:\s*([A-Za-z0-9.+-]+)", head)
        if not m:
            continue
        got = m.group(1)
        s, pat = dep5_stanza_for(f, stanzas)
        if s is None:
            out.append((f, 0, "no Files stanza in packaging/debian/copyright matches this path"))
            continue
        if s["license"] != got:
            out.append(
                (f,
                    line_of(head, m.start()),
                    f"header says {got}, but packaging/debian/copyright declares {s['license']} "
                    f"for it (matched by 'Files: {pat}')",
                )
            )
        blob = " ".join(s["copyright"])
        for holder in re.findall(r"SPDX-FileCopyrightText:\s*(.+)", head):
            # Drop the comment syntax the header sits inside, drop the address
            # the stanza spells out and the header does not, and drop the leading
            # year span. Then compare on the name alone, which is the only part
            # of a holder this rule is in a position to check.
            #
            # The year has to go. A stanza written "2024-2026 fuddlesworth" is a
            # DEP-5 range covering every file it matches, so a header's single
            # year is not a substring of it except by accident: "2026" happens to
            # end the range and passes, while 2025 and 2027 do not. Comparing the
            # years would mean teaching this rule to expand DEP-5 ranges, and a
            # year that has merely fallen out of date is not a licensing defect.
            # What matters to a redistributor is that every holder named in a
            # file is also named in the stanza that covers it.
            #
            # The prefix accepts the SPDX-legal spellings, not just a bare year: a leading
            # "(c)", "©" or "Copyright" WITH OR WITHOUT a year after it, an en-dash range
            # as well as a hyphen, and "YYYY-present". Every header in the tree today is a
            # bare year, a YYYY-YYYY range or a bare name, so none of that is exercised
            # here — but each unhandled spelling is a FALSE POSITIVE waiting for the first
            # file that uses it, which is the same shape as the year bug itself.
            #
            # BOTH optional groups, the lookahead and the REPETITION are load-bearing.
            # Requiring the year meant "(c) fuddlesworth" kept its prefix and then failed the
            # blob test. Making the year optional WITHOUT the lookahead is worse: the prefix
            # then matches the leading letters of an ordinary name, and "Copyrighteous Inc"
            # strips to "eous Inc". The lookahead only lets the prefix go when a space, a digit
            # or the end follows it. And the group REPEATS, because the prefix stacks in the
            # commonest spelling of all: "Copyright (c) 2026 Name" carries two, and matching
            # one left "(c) 2026 Name" to fail the blob test — a blocking false positive on
            # the exact form an upstream header gets pasted in with.
            name = re.sub(r"\s*(-->|\*/|\",?)\s*$", "", holder.strip()).split("<")[0].strip()
            name = re.sub(
                r"^(?:(?:\(c\)|©|Copyright)(?=\s|\d|$)\s*)*(?:\d{4}(\s*[-–,]\s*(?:\d{4}|present))*)?\s*",
                "", name, flags=re.IGNORECASE)
            name = name.strip()
            if name and name not in blob:
                out.append(
                    (f,
                        0,
                        f"copyright holder {name!r} is not named in the matching "
                        f"packaging/debian/copyright stanza ('Files: {pat}')",
                    )
                )
    return out
