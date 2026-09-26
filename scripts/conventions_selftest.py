# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-test for check-conventions.py's two pure detectors and its two split rules.

Split out of the gate itself, which crossed the 1150-line ceiling when two
branches each landed a new rule. This is the most separable concern in it: test
DATA plus one function, depending on nothing but `prose_problems` and
`iter_json_prose`.

Imported lazily by the gate's `--selftest` arm rather than importing it at module
scope, so the two files cannot form a cycle. The gate keeps the flag, because
lefthook and CI both invoke it by name.

The two sibling rule modules are imported DIRECTLY rather than passed in, which the
detectors above cannot be. Neither imports the gate, so there is no cycle to avoid,
and being importable at all was the point of splitting them out.
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path


#
# WHY THIS EXISTS. The prose rule shipped unable to see data/whatsnew.json's
# highlights, which CLAUDE.md names explicitly. The detector was fine; the JSON
# walker never handed it those strings, because they are bare elements of an array
# and it keyed on the array INDEX. Nothing pinned either half, so the gate printed
# a clean result over unread text and would have gone on doing so.
#
# Both halves are pure functions, so they are pinned here rather than in a fake
# repo tree: the detector's positive and negative shapes, and the walker's key
# resolution. A rule that narrows now fails instead of going quiet.
#
# This does NOT replace exercising a rule end to end against a planted violation,
# which is still the right way to check a new rule. It closes the part that can rot
# without anyone touching the rule at all.

SELFTEST_PROSE_BAD = [
    ("Blurs the pane — and lifts saturation.", "em-dash"),
    ("The pane is blurred; the border is not.", "semicolon"),
    ("Blur radius - in logical pixels.", "spaced hyphen"),
    # A SPLICE WITH A COMMA IN IT. The comma test used to look at the whole string
    # rather than the item either side of the semicolon, so any comma anywhere
    # suppressed the check and this went unreported. The first version of this
    # selftest missed it because none of its probes had a comma, which is the
    # lesson: a selftest is only as good as the shapes it names.
    ("The pane, when focused, is blurred; the border is not.", "semicolon past a comma"),
    # An appositive long enough not to read as a label.
    ("Blurs the scene behind the pane — a soft look that lifts saturation.", "em-dash appositive"),
    # A SPLICE WITH A COMMA ON EACH SIDE, which is what the per-item comma carve-out
    # used to exempt and is the exact shape that got a real violation into a shipped
    # pack description. Without this entry the carve-out can be put back and the
    # selftest still passes, since every other guard in the rule is pinned by an OK
    # entry and only this direction is pinned by a BAD one.
    ("It scales with the frame, so it is larger on a larger window; with it, the bend is confined to the bevel",
     "semicolon with a comma on each side"),
    # A SPLICE WHOSE SECOND CLAUSE IS A NOUN PHRASE PLUS A COPULA, the shape the
    # finite-verb test has to keep catching now that it lets verbless items through.
    ("The captured pane is blurred at half density; the border pack is drawn over it",
     "semicolon between two copular clauses"),
    # A THREE-CLAUSE splice, which the removed semicolon-count short-circuit made
    # permanently invisible. Pins that the count heuristic stays gone.
    ("The pane is blurred; the border is not; the shadow stays.", "three-clause splice"),
    # A splice built from LEXICAL verbs rather than auxiliaries. This exact shape
    # shipped in five schema descriptions, so the verb list has to reach past the
    # copulas far enough to see it.
    ("so it keeps the pack's default; the loader drops the entry", "semicolon between two lexical-verb clauses"),
]

SELFTEST_PROSE_OK = [
    # A genuine list. THREE items means two semicolons, which is the enumeration
    # signal the rule returns early on. Pins that early return: without it the final
    # item, which carries no comma of its own, reads as a clause and is flagged.
    "Sets the width, in pixels; the radius, in pixels; and the colour",
    # A two-item list, which has only ONE semicolon and so gets no enumeration
    # signal. It survives on the three-word clause floor, and that is what this entry
    # pins: drop the floor and a two-word pair reads as a splice.
    "Left, top; right, bottom",
    # TWO-ITEM LISTS LONG ENOUGH TO CLEAR THE WORD FLOOR, which CLAUDE.md permits
    # without naming a minimum count and which only the finite-verb test lets
    # through. Neither item carries a verb; both carry the internal comma the
    # carve-out is written for. Drop the verb test and both are flagged.
    "Sets the width, in pixels; the radius, in logical pixels",
    "Radius, in logical pixels; Strength, a unitless multiplier",
    # A "#"-led line is a shell comment in pasteable terminal text, which CLAUDE.md
    # puts out of scope along with the rest of the code. Pins the skip.
    "Run it like this:\n# plasmazones --rules a - b\nThen restart.",
    # A literal separator between two nouns, which CLAUDE.md allows.
    "%1 — %2",
    # Semicolons inside backticked code.
    "Run `a = 1; b = 2` first.",
    # A spaced hyphen inside backticked code.
    "Pass `--rules a - b` to narrow it.",
    # Two sentences, which is the prescribed rewrite.
    "Blurs the pane. It also lifts saturation.",
    # FORWARD GUARDS, not coverage. CLAUDE.md also forbids a dramatic "Label: payload"
    # colon, and allows a settings breadcrumb, but prose_problems implements neither a
    # colon rule nor an arrow rule, so neither of these can fail under any mutation of
    # what is implemented. They are here so that a colon rule added later has its two
    # legitimate shapes already pinned.
    "Settings → Snapping",
    "Radius: 24",
]

SELFTEST_JSON = json.dumps(
    {
        "releases": [{"version": "1.0", "highlights": ["Fixed: the pane blurs — and the border follows."]}],
        "description": "A description field.",
        "presets": {"Plasma": {}},
        "nested": {"notprose": ["ignored — not a prose key"]},
    }
)


#
# WHY THE TWO SECTIONS BELOW EXIST. The prose detector above is pinned; the two rules
# that were split into their own modules were not, and between them they produced a
# defect in five consecutive review rounds, every one in code written to fix the
# previous one. Two patterns recurred and neither was reachable by any gate: a fix
# applied to ONE SIDE of a symmetric pair, four times over, and a coverage floor that
# counted the wrong thing three times. Both are exactly what a planted case catches
# and no amount of re-reading does.
#
# These run against a FAKE TREE in a temporary directory, not the repo, so they pin
# behaviour rather than today's data. A case that asserts something about the 24 real
# surface packs would start failing the day a pack is added.


def _pack(d: Path, name: str, *, param="roundBottomCorners", desc="Rounds the bottom corners.",
          declare=True, raw=None) -> Path:
    """One fake surface pack. `raw` writes the file verbatim, for the malformed shapes."""
    p = d / "packs" / name
    p.mkdir(parents=True, exist_ok=True)
    meta = p / "metadata.json"
    if raw is not None:
        meta.write_text(raw, encoding="utf-8") if isinstance(raw, str) else meta.write_bytes(raw)
        return meta
    params = []
    if declare:
        entry = {"id": param, "name": "Round bottom corners", "type": "bool", "default": False}
        if desc is not _OMIT:
            entry["description"] = desc
        params.append(entry)
    meta.write_text(json.dumps({"parameters": params}), encoding="utf-8")
    return meta


_OMIT = object()  # "write no description key at all", distinct from None and from ""


def _shared_text_failures() -> list[str]:
    import conventions_shared_text as mod

    bad: list[str] = []
    glob = "packs/*/metadata.json"

    def run(build) -> list[tuple[str, str]]:
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            build(d)
            saved = mod.SHARED_PARAM_TEXT
            mod.SHARED_PARAM_TEXT = {"roundBottomCorners": glob}
            try:
                return mod.shared_param_problems(d)
            finally:
                mod.SHARED_PARAM_TEXT = saved

    def expect(label, build, want: bool, needle: str | None = None):
        found = run(build)
        hit = [m for _, m in found]
        if want and not hit:
            bad.append(f"shared_param_problems missed {label}")
        elif not want and hit:
            bad.append(f"shared_param_problems false-positived on {label}: {hit}")
        elif want and needle and not any(needle in m for m in hit):
            bad.append(f"shared_param_problems reported {label} but no message said {needle!r}: {hit}")

    # Agreement is silent, and disagreement is not. The two directions together are
    # what a mutation to the comparison has to break.
    expect("two packs agreeing", lambda d: (_pack(d, "a"), _pack(d, "b")), False)
    expect("two packs disagreeing",
           lambda d: (_pack(d, "a"), _pack(d, "b", desc="Rounds the bottom corners too.")),
           True, "first differs at 0-based character")

    # THE FLOOR, both halves, split apart in the fourth version. Too few paths is a
    # stale glob; no text anywhere is a mistyped id. Each has to fire on its own.
    expect("a glob matching one path", lambda d: _pack(d, "only"), True, "stale")
    expect("three packs declaring the id and none describing it",
           lambda d: [_pack(d, n, desc=_OMIT) for n in ("a", "b", "c")],
           True, "not one carries")
    expect("packs that declare nothing at all",
           lambda d: [_pack(d, n, declare=False) for n in ("a", "b", "c")],
           True, "not one carries")

    # `"description": null`. str()-ing it made the bucket key the literal word "None",
    # which counted as text and let an all-null set pass the floor. This is the one case
    # in this file's history that a written test caught before review did.
    expect("three packs whose description is JSON null",
           lambda d: [_pack(d, n, desc=None) for n in ("a", "b", "c")],
           True, "not one carries")
    # And whitespace-only, which `not text` used to pass straight into the drift arms
    # so that " " was quoted back as a competing wording.
    expect("three packs whose description is a single space",
           lambda d: [_pack(d, n, desc="   ") for n in ("a", "b", "c")],
           True, "not one carries")

    # THE ONE-SIDED PAIR, fifth instance of that shape and the reason the reference is
    # now chosen among packs that say something. Ten silent packs outvoted two real
    # wordings, both were told they were the only text in the tree, and the drift
    # between them went unreported. The A-vs-B drift is the assertion that matters.
    def mixed(d):
        for n in range(10):
            _pack(d, f"silent{n}", desc=_OMIT)
        _pack(d, "ay1", desc="Wording A.")
        _pack(d, "ay2", desc="Wording A.")
        _pack(d, "bee1", desc="Wording B.")
        _pack(d, "bee2", desc="Wording B.")

    found = run(mixed)
    msgs = [m for _, m in found]
    # The three arms that describe a real disagreement. Not matched on the wording text
    # itself: these messages quote from the FIRST DIFFERING CHARACTER, so "Wording A."
    # against "Wording B." is reported as 'A.' against 'B.' and the word never appears.
    if not any(("first differs" in m) or ("TRUNCATED" in m) or ("EXTENDED" in m) for m in msgs):
        bad.append("shared_param_problems never reported the drift between two real wordings "
                   f"when most packs were silent (the one-sided-pair shape): {msgs}")
    if any("is the only text here" in m for m in msgs):
        bad.append("shared_param_problems still tells a pack it is the only text while another "
                   f"pack holds different text: {msgs}")

    # ONE pack with text beside packs that omit it is NOT a rule with nothing to
    # compare. The third floor threw this finding away and called it "compared against
    # nothing" while holding two buckets.
    def one_with_text(d):
        _pack(d, "has", desc="Wording A.")
        _pack(d, "lacks1", desc=_OMIT)
        _pack(d, "lacks2", desc=_OMIT)

    msgs = [m for _, m in run(one_with_text)]
    if not any("declares no description" in m for m in msgs):
        bad.append(f"shared_param_problems dropped the omissions beside a pack that has text: {msgs}")
    if any("no-op" in m for m in msgs):
        bad.append(f"shared_param_problems called a comparable set a no-op: {msgs}")

    # MALFORMED PACKS PRODUCE FINDINGS, NEVER TRACEBACKS. Each of these escaped the
    # rule once and took every other rule in the invocation down with it.
    expect("a pack whose parameters is null",
           lambda d: (_pack(d, "a"), _pack(d, "b"), _pack(d, "bad", raw='{"parameters": null}')),
           False)
    expect("a pack whose root is an array",
           lambda d: (_pack(d, "a"), _pack(d, "b"), _pack(d, "bad", raw="[]")),
           False)
    expect("a pack with a non-string description",
           lambda d: (_pack(d, "a"), _pack(d, "b"), _pack(d, "num", desc=7)),
           True, "non-string")
    # A BOM'd pack must PARTICIPATE, not drop out: read as plain utf-8 it raises, and
    # swallowing that lowers the count the message reports.
    expect("a BOM'd pack that agrees",
           lambda d: (_pack(d, "a"),
                      _pack(d, "bom", raw='﻿{"parameters": [{"id": "roundBottomCorners", '
                                          '"description": "Rounds the bottom corners."}]}')),
           False)
    expect("a pack that is not valid UTF-8",
           lambda d: (_pack(d, "a"), _pack(d, "b"),
                      _pack(d, "raw", raw=b'{"parameters": [{"id": "x", "description": "\xff\xfe"}]}')),
           True, "not valid UTF-8")

    # A SYMLINKED pack is one piece of evidence, not two, so it must not satisfy the
    # floor on its own. Skipped where symlinks are not available.
    def symlinked(d):
        real = _pack(d, "real")
        link = d / "packs" / "mirror"
        link.mkdir(parents=True, exist_ok=True)
        os.symlink(real, link / "metadata.json")

    try:
        with tempfile.TemporaryDirectory() as probe:
            os.symlink(probe, Path(probe) / "l")
    except (OSError, NotImplementedError):
        pass
    else:
        expect("a pack symlinked to itself twice", symlinked, True, "stale")

    # AN UNREADABLE PACK IS REPORTED, not dropped. The gate's main() filters unreadable
    # TRACKED paths, but this rule globs its own files, so an untracked pack never passes
    # through that filter and a silent skip would lower the count the message reports.
    def unreadable(d):
        _pack(d, "a")
        _pack(d, "b")
        (_pack(d, "locked")).chmod(0o000)

    with tempfile.TemporaryDirectory() as probe:
        p = Path(probe) / "x"
        p.write_text("x", encoding="utf-8")
        p.chmod(0o000)
        try:
            p.read_text(encoding="utf-8")
        except OSError:
            root = False
        else:
            root = True  # running as root, where the mode is not enforced
        p.chmod(0o600)
    if not root:
        expect("a pack that cannot be read", unreadable, True, "cannot be read")

    # An EMPTY table is the cheapest way to switch the rule off while leaving it listed,
    # so it must announce itself rather than pass.
    with tempfile.TemporaryDirectory() as tmp:
        saved = mod.SHARED_PARAM_TEXT
        mod.SHARED_PARAM_TEXT = {}
        try:
            if not mod.shared_param_problems(Path(tmp)):
                bad.append("shared_param_problems passes silently with an empty SHARED_PARAM_TEXT")
        finally:
            mod.SHARED_PARAM_TEXT = saved

    return bad


def _dep5_failures() -> list[str]:
    import conventions_dep5 as mod

    # line_of only decorates the licence-mismatch message, and reimplementing it here
    # would be testing this file against itself, so it is a stub. The assertions below
    # are all about WHICH findings appear, never about their line numbers.
    def line_of(text, index):
        return text[:index].count("\n") + 1

    bad: list[str] = []
    stanza = (
        "Files: *\n"
        "Copyright: 2024-2026 fuddlesworth <fuddlesworth@users.noreply.github.com>\n"
        "License: GPL-3.0-or-later\n"
        "\n"
        "Files: libs/*\n"
        "Copyright: 2024-2026 fuddlesworth <fuddlesworth@users.noreply.github.com>\n"
        "License: LGPL-2.1-or-later\n"
    )

    def run(files: dict[str, str | bytes], *, dep5: str | None = stanza, check=None):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            if dep5 is not None:
                (d / "packaging" / "debian").mkdir(parents=True)
                (d / "packaging" / "debian" / "copyright").write_text(dep5, encoding="utf-8")
            for rel, body in files.items():
                p = d / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(body) if isinstance(body, bytes) else p.write_text(body, encoding="utf-8")
            targets = list(check) if check is not None else list(files)
            return [m for _, _, m in mod.dep5_problems(
                targets, repo=d, line_of=line_of,
                tracked_files=lambda: list(files) + ["packaging/debian/copyright"])]

    def hdr(year="2026", who="fuddlesworth", lic="GPL-3.0-or-later"):
        return f"// SPDX-FileCopyrightText: {year} {who}\n// SPDX-License-Identifier: {lic}\n"

    # THE YEAR BOMB. A DEP-5 range covers every file it matches, so a header's single
    # year is not a substring of "2024-2026" except by accident: 2026 ends the range and
    # passed, while 2025 and 2027 did not. The next file added in a new year would have
    # broken the gate for a reason no reader would guess from the message.
    for year in ("2024", "2025", "2026", "2027", "2024-2026"):
        got = run({"src/a.cpp": hdr(year=year)})
        if got:
            bad.append(f"dep5_problems fired on a header dated {year} against a 2024-2026 stanza: {got}")
    # It still has to notice a holder the stanza really does not name.
    if not any("not named" in m for m in run({"src/a.cpp": hdr(who="Some Stranger")})):
        bad.append("dep5_problems missed a copyright holder absent from the stanza")
    # And the year strip must not swallow the whole holder when the name IS a year-like
    # token, nor fire when a holder legitimately carries a parenthesised project.
    if not any("not named" in m for m in run({"src/a.cpp": hdr(who="Simon Schneegans (Burn-My-Windows)")})):
        bad.append("dep5_problems missed an upstream holder absent from the stanza")

    # The licence split, in both directions, including last-match-wins: libs/* is
    # declared after * and governs.
    if not any("declares" in m for m in run({"src/a.cpp": hdr(lic="LGPL-2.1-or-later")})):
        bad.append("dep5_problems missed a header/stanza licence mismatch")
    if run({"libs/phosphor-x/a.cpp": hdr(lic="LGPL-2.1-or-later")}):
        bad.append("dep5_problems ignored last-match-wins and used the first matching stanza")

    # A path no stanza covers is the drift this rule was written for.
    if not any("no Files stanza" in m for m in
               run({"src/a.cpp": hdr()}, dep5="Files: other/*\nCopyright: x\nLicense: MIT\n")):
        bad.append("dep5_problems missed a path that no Files stanza matches")

    # Shapes it must pass over rather than choke on: a binary, a file with no SPDX
    # header at all (that is the spdx rule's finding, not this one), and an absent
    # DEP-5 file, which means the check cannot run rather than that everything failed.
    if run({"data/x.png": b"\x89PNG\r\n\x1a\n\x00\x01"}):
        bad.append("dep5_problems reported a binary asset")
    if run({"src/plain.cpp": "int main() { return 0; }\n"}):
        bad.append("dep5_problems reported a file carrying no SPDX header")
    if run({"src/a.cpp": hdr(lic="MIT")}, dep5=None):
        bad.append("dep5_problems reported findings with no packaging/debian/copyright present")

    # Editing the DEP-5 file itself widens the check to the whole tree, because that one
    # edit can break any file in it. Passing ONLY the copyright path must still reach a
    # mismatching source file.
    widened = run({"src/a.cpp": hdr(lic="MIT")}, check=["packaging/debian/copyright"])
    if not any("declares" in m for m in widened):
        bad.append(f"dep5_problems did not widen to the tree when the copyright file itself changed: {widened}")

    return bad


def run_selftest(prose_problems, iter_json_prose) -> int:
    """The two detectors arrive as arguments, so this module never imports the
    gate at module scope and the pair cannot form a cycle."""
    failures = []

    for text, shape in SELFTEST_PROSE_BAD:
        if not prose_problems(text):
            failures.append(f"prose_problems missed the {shape} shape: {text!r}")
    for text in SELFTEST_PROSE_OK:
        found = prose_problems(text)
        if found:
            failures.append(f"prose_problems false-positived on {text!r}: {found}")

    with tempfile.TemporaryDirectory() as d:
        probe = Path(d) / "probe.json"
        probe.write_text(SELFTEST_JSON, encoding="utf-8")
        seen = {trail: s for trail, s in iter_json_prose(str(probe))}
        # The shape the rule used to miss: a bare string inside an array whose KEY
        # is a prose key. Keyed on the index, this never arrived. The trail ends at
        # the ARRAY's key, not at the element's position, because walk() carries the
        # list's trail down to a string element rather than appending its index.
        if "/releases/0/highlights" not in seen:
            failures.append("iter_json_prose skips a prose string held as an array element (the whatsnew shape)")
        if "/description" not in seen:
            failures.append("iter_json_prose skips a plain prose field")
        # A preset's key is its label.
        if not any(v == "Plasma" for v in seen.values()):
            failures.append("iter_json_prose skips a preset name, which the picker shows verbatim")
        # And it must NOT widen to every string in the document.
        if any("notprose" in trail for trail in seen):
            failures.append("iter_json_prose yields strings under a non-prose key")

    failures.extend(_shared_text_failures())
    failures.extend(_dep5_failures())

    for line in failures:
        print(f"selftest: {line}", file=sys.stderr)
    if failures:
        print(f"\n{len(failures)} selftest failure(s)", file=sys.stderr)
        return 1
    print("selftest: ok")
    return 0
