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
    #
    # The widened list deliberately BYPASSES the gate's readability precondition, which
    # filtered `files` before this rule was called. That is safe here and nowhere else:
    # dep5_head returns None for a path it cannot read, so an unreadable file is skipped
    # rather than handed to the rest of this function as an empty string. What the bypass
    # costs is the `[unreadable]` finding for a file the caller never named, which on a
    # pre-commit run touching only the DEP-5 file would be noise rather than news.
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
        # `[ \t]*`, not `\s*`, because `\s` crosses a NEWLINE: a tag with an empty value
        # captured the FOLLOWING line, so a header naming no holder reported its own
        # SPDX-License-Identifier line as the holder — the exact misquote the raw-holder arm
        # below exists to prevent. `(.*)` keeps the empty value matching, so it still reaches
        # the names-no-holder arm rather than vanishing from the scan.
        for holder in re.findall(r"SPDX-FileCopyrightText:[ \t]*(.*)", head):
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
            #
            # The lookahead admits `(` and the sign as well as a space or digit, because
            # "Copyright(c)" and "Copyright©2026" are written without one. The year run ends
            # with an OPTIONAL LONE SEPARATOR because its own repeating separator consumes one
            # only when another year follows, so "2026, Acme Inc", "2015- Acme Inc" and
            # "2026 - Acme Inc" each kept their punctuation and reported it as part of the
            # holder. That separator sits INSIDE the year group on purpose: a name that
            # legitimately opens with a hyphen and carries no year keeps it. `by` sits inside
            # that same group, so it strips only when a YEAR precedes it, which is the one shape
            # it exists for ("Copyright (c) 2026 by Acme Inc"). THE MOVE COSTS the year-less
            # forms: "by Acme Inc", "Copyright by Acme Inc" and "(c) by Acme Inc" keep their
            # "by" now and would be reported with it. That is accepted, because with no year
            # "by X" and a company actually named "By X" are textually identical and no
            # lookahead can separate them, so the alternative is the over-strip direction.
            # The PREFIX words keep that same whole-word hazard and cannot be moved the same
            # way ("Copyright" alone is a pinned must-not-fire spelling), which is why
            # "Copyright Clearance Center" still over-strips and the message quotes the raw
            # holder below. Outside the group `by` also ate the
            # first word of any name beginning with those letters, and its lookahead does not
            # prevent that: it guards "Bystander" and "byte Foundation", where `by` is a word
            # PREFIX, but not "By The Way Inc", where it is a whole word. Each of the last few
            # rounds closed one sibling of this shape, which is why every spelling is pinned in
            # the self-test rather than argued about here. Knowingly out of scope is punctuation
            # directly after the prefix WORD, since the lookahead class admits neither: the
            # malformed "Copyright: 2026 Name", and also the ordinary "Copyright, 2026 Name",
            # which is a plain spelling rather than a malformed one and is simply not handled.
            #
            # ONLY UNDER-stripping can raise a FALSE POSITIVE, and that is what bounds the risk
            # of every change to this pattern. It is anchored at ^, so whatever survives is a
            # SUFFIX of what the file says, and a stanza that names the same holder contains
            # that suffix as well. Over-stripping is not free either, and the cost is NOT
            # message accuracy: it is SILENT, and it drops a real FINDING, because the
            # containment test only gets laxer as the survivor shortens and a short enough
            # survivor turns up inside some longer word of the stanza. The two directions fail
            # differently and neither is safe to trade away for the other.
            name = re.sub(r"\s*(-->|\*/|\",?)\s*$", "", holder.strip()).split("<")[0].strip()
            name = re.sub(
                r"^(?:(?:\(c\)|©|Copyright)(?=[\s(©\d]|$)\s*)*"
                r"(?:\d{4}(?:\s*[-–,]\s*(?:\d{4}|present))*(?:\s*[-–,])?\s*(?:by(?=\s)\s*)?)?"
                r"\s*,?\s*",
                "", name, flags=re.IGNORECASE)
            name = name.strip()
            raw = holder.strip()
            if not name:
                # The holder was nothing but a prefix word, a year, or punctuation, so the
                # pattern consumed the whole line (the lookahead's `|$` branch is what lets it
                # eat a bare "Copyright"). An empty survivor cannot be looked up in the stanza,
                # so this used to fall through in silence, and nothing ELSE in the gate reads
                # the holder: rule_spdx only checks that the tag is present. A copyright line
                # naming nobody is a DEP-5 defect of its own, so it is reported here. The raw
                # value can now be EMPTY too, since the capture stops at the line end rather
                # than running into the next one, so the message has to read either way.
                out.append((f, 0, "copyright line names no holder"
                                  + (f", only {raw!r}" if raw else " at all")))
                continue
            if name and name not in blob:
                # Quote the RAW holder too when the strip changed it. The strip is anchored
                # at ^, so what is left is always a suffix of what the file says — and for a
                # name that legitimately opens with one of the prefix words, "Copyright
                # Clearance Center" was reported as 'Clearance Center', a string the author
                # cannot find in their file. Same rule as the non-string description guard in
                # conventions_shared_text.py: never quote back a value the file does not hold.
                shown = f"{name!r}" if name == raw else f"{name!r} (the header says {raw!r})"
                out.append(
                    (f,
                        0,
                        f"copyright holder {shown} is not named in the matching "
                        f"packaging/debian/copyright stanza ('Files: {pat}')",
                    )
                )
    return out
