# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""The DEP-5 copyright rule for check-conventions.py.

Split out of the gate for the reason its two siblings record: the gate reached its
1150-line ceiling, and CLAUDE.md says to split past it rather than shave comments.
This is the most separable rule — self-contained, the only one that walks every
tracked path rather than a suffix-filtered subset, and dependent on nothing but three
gate helpers, which arrive as keyword arguments the way run_selftest's do.

Returns plain (path, line, message) tuples rather than the gate's Violation, so the
two files cannot form an import cycle.
"""
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
    suffix-filtered subset, so it meets what the others never see: the symlinked
    skill directories under .agents/, and binary assets. Reading a bounded slice
    also keeps a whole-tree run cheap."""
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


def dep5_problems(files, *, repo, read, line_of, tracked_files):
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
            # Drop the comment syntax the header sits inside, then compare on
            # the name alone: the stanza spells fuddlesworth with an address.
            name = re.sub(r"\s*(-->|\*/|\",?)\s*$", "", holder.strip()).split("<")[0].strip()
            if name and name not in blob:
                out.append(
                    (f,
                        0,
                        f"copyright holder {name!r} is not named in the matching "
                        f"packaging/debian/copyright stanza ('Files: {pat}')",
                    )
                )
    return out
