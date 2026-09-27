# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""The plain-prose detector: one pure function over a string, plus its data.

Split out of check-conventions.py, which had reached the 1150-line hard ceiling that
CLAUDE.md sets, so every edit to it had to be exactly line-neutral and three audit
rounds running had to pay that cost to correct a comment. This is the most separable
concern left after dep5, the shared-param text and the selftest: `prose_problems` reads
no file, resolves no path and imports nothing from the gate, so the split cannot form a
cycle and the gate passes the function to the selftest exactly as before.

What the detector decides, and what it deliberately leaves to review, is written on
`prose_problems` itself. The rule that CALLS it -- which files it reaches, and how each
surface's strings are extracted -- stays in the gate beside the other rules.
"""
from __future__ import annotations

import re

# A literal typographic separator between two nouns is explicitly allowed (the
# "%1 — %2" Layout/Zone display format), and so are settings-path breadcrumbs.
SENTENCE_END = re.compile(r"[.!?](?:\s|$)")


def is_title_separator(s: str) -> bool:
    """True for the allowed "<noun phrase> — <noun phrase>" display format.

    CLAUDE.md permits a literal typographic separator between two nouns, the
    canonical case being the "%1 — %2" Layout/Zone format. The test is that
    the string is a label rather than prose: exactly one em-dash, no sentence
    punctuation on either side, a short noun phrase each side, no finite verb
    on either side, and a right side that does not open with a conjunction.

    The sentence-end test alone used to carry this, and every BAD probe in the
    self-test ends in a period, so the word cap was never exercised: an
    unpunctuated splice with five words a side was exempted outright. That is the
    shape of a JSON `name`, a .desktop Comment and a short tr() label.

    STILL UNDER-CATCHES, deliberately, in the same direction as the semicolon
    arm. A verbless appositive with no conjunction ("Round the corners — a softer
    look") reads exactly like a label to every test here, and review has to catch
    it. Erring toward silence is the right way round for a pre-commit gate; the
    alternative blocks a legitimate "%1 — %2".
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
        if _has_finite_verb(side):
            return False
    # A label's second half names a thing. Opening with a coordinating conjunction
    # makes it a continuation of the first clause, which is the splice CLAUDE.md
    # forbids rather than the separator it allows.
    tail = parts[1].strip().split()
    if tail and tail[0].strip(".,:;!?()[]\"'").lower() in {"and", "but", "or", "so", "nor", "yet"}:
        return False
    return True


# Finite-verb forms, for "does this segment read as a CLAUSE rather than a list
# item". Auxiliaries and copulas, plus the third-person-singular lexical verbs this
# project's prose actually uses.
#
# A LONGER LIST CANNOT CREATE A FALSE POSITIVE HERE, which is why it can afford to
# grow: the semicolon rule requires a finite verb on BOTH sides, and a genuine
# comma-bearing list item is a noun phrase with no verb at all, so one verbless side
# is enough to exempt the whole construction. The list only affects how many real
# splices get caught. It is still a heuristic and still under-catches — a splice
# built from verbs not named here reads as a list and is missed, which review has to
# catch — but it errs toward silence rather than toward blocking a legitimate
# sentence, which is the right way round for a pre-commit gate.
_FINITE_VERBS = frozenset(
    """is are was were am be been being has have had do does did
       can cannot could will would shall should may might must
       isn't aren't wasn't weren't hasn't haven't doesn't don't didn't
       can't won't wouldn't shouldn't
       keeps drops sets reads writes runs takes gives makes shows uses needs holds
       adds stops starts applies returns means covers carries leaves gets goes comes
       sits lands falls picks sends pushes pulls draws paints binds clears
       stays remains lets allows follows treats wins counts names points works
       scales fades bends""".split()
)


def _has_finite_verb(segment: str) -> bool:
    """Whether `segment` reads as a CLAUSE rather than a list item."""
    return any(w.strip(".,:;!?()[]\"'").lower() in _FINITE_VERBS for w in segment.split())


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
    # NORMALISE the entity before the carve-out, not just before the test that finds it.
    # The arm below looked for either spelling but is_title_separator splits on the literal
    # dash only, so "%1 &mdash; %2" yielded ONE part, failed the len==2 test and was
    # reported — a false positive on a shape CLAUDE.md explicitly allows.
    #
    # THREE spellings, because `&mdash;` is an HTML entity and the AppStream XML this rule
    # reads declares no entities, so that spelling would actually make the document
    # ill-formed and appstreamcli would reject it. The two spellings that ARE well-formed
    # XML are the numeric references, and they were the ones going unnormalised. None of
    # the three occurs in the tree today; this is forward cover, and the selftest carries a
    # bad probe per spelling so it stays cover rather than becoming decoration.
    core = without_code.replace("&mdash;", "—").replace("&#8212;", "—").replace("&#x2014;", "—").strip()
    if "—" in core:
        if not is_title_separator(core):
            problems.append("em-dash splice; write two sentences or join with a plain word")
    if " - " in without_code:
        problems.append("spaced hyphen used as a dash; rewrite the sentence")
    # CLAUSE-SPLICING SEMICOLON. CLAUDE.md forbids one joining two independent
    # clauses and permits one "separating genuine comma-bearing list items", naming
    # no minimum item count, so the test has to tell those two shapes apart directly.
    #
    # SEGMENT FIRST. The pair this rule judges is the one around THIS semicolon, and
    # testing against the whole string meant anything found anywhere in a
    # multi-paragraph block (an RPM %description, a Nix longDescription, a CHANGELOG
    # entry) decided the verdict for every sentence in it.
    #
    # Then, inside a segment, key on a FINITE VERB present on BOTH sides. A clause
    # has one; a list item is a noun phrase and has none, which is why "Sets the
    # width, in pixels; the radius, in logical pixels" is a list — its second item
    # carries no verb at all.
    #
    # That replaces two heuristics that stood in for it. A COMMA test cannot tell a
    # list item from a clause with a parenthetical: "The pane, when focused, is
    # blurred; the border is not." has a comma on each side and is a textbook splice,
    # and so did a real description that shipped. A WORD-COUNT floor guarded against
    # a short fragment reading as a clause, which the verb test now does directly at
    # any length. A SEMICOLON-COUNT short-circuit, exempting anything with two or
    # more, bought a three-item list its exemption at the price of never seeing a
    # three-CLAUSE splice.
    #
    # The verb list is a heuristic and it under-catches: a splice built from verbs it
    # does not name reads as a list and is missed, for review to catch. It cannot
    # over-catch, because one verbless side exempts the construction and a genuine
    # list item has no verb — which is what lets the list grow safely.
    # \s* rather than \s+: a splice written without a space after the semicolon is
    # still a splice, and requiring one let "blurred;the border" through.
    for segment in re.split(r"(?<=[.!?])\s+|\n\s*\n", without_code):
        # The optional bracket/quote class matters: a second clause opening with one
        # ("; (the border is not)") had no word character where the arm looked, so it
        # read as no semicolon at all. Non-capturing, because only the match POSITION
        # is used and the old group was never read.
        for part in re.finditer(r";\s*[(\[\"']*\s*\w", segment):
            before = segment[: part.start()]
            after = segment[part.start() + 1 :]
            if _has_finite_verb(before) and _has_finite_verb(after):
                problems.append("clause-splicing semicolon; split into sentences or use \"and\"")
                return problems
    return problems
