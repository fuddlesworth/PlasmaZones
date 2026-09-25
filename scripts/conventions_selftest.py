# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-test for check-conventions.py's two pure detectors.

Split out of the gate itself, which crossed the 1150-line ceiling when two
branches each landed a new rule. This is the most separable concern in it: test
DATA plus one function, depending on nothing but `prose_problems` and
`iter_json_prose`.

Imported lazily by the gate's `--selftest` arm rather than importing it at module
scope, so the two files cannot form a cycle. The gate keeps the flag, because
lefthook and CI both invoke it by name.
"""
import json
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

    for line in failures:
        print(f"selftest: {line}", file=sys.stderr)
    if failures:
        print(f"\n{len(failures)} selftest failure(s)", file=sys.stderr)
        return 1
    print("selftest: ok")
    return 0


