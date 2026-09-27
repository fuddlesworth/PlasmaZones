# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-test for check-conventions.py's two pure detectors, its two split rules and its
readability precondition.

Split out of the gate itself, which crossed the 1150-line ceiling when two
branches each landed a new rule. This is the most separable concern in it: test
DATA plus one function, depending on nothing it is not handed: the two detectors
(`prose_problems`, `iter_json_prose`) and the readability precondition
(`partition_readable`), through whose `__globals__` the arms that need the gate's own
namespace reach `rule_spdx`, `CODE_SUFFIXES`, `SPDX_HEAD_LINES` and `REPO`.

Imported lazily by the gate's `--selftest` arm rather than importing it at module
scope, so the two files cannot form a cycle. The gate keeps the flag, because
lefthook and CI both invoke it by name.

The two sibling rule modules are imported DIRECTLY rather than passed in, which the
detectors above cannot be. Neither imports the gate, so there is no cycle to avoid,
and being importable at all was the point of splitting them out.

WHAT IS HERE AND WHAT IS NOT. This file holds the PURE-DETECTOR probes: test data plus
assertions about `prose_problems`, `iter_json_prose` and the shared-text and dep5
detectors, none of which builds a tree. The arms that drive a real RULE over a fake tree
live in conventions_selftest_rules.py and arrive through one `rule_coverage_failures`
call. They were split out when this file crossed the 1150-line ceiling, along the line
those two concerns already fell on.
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

from conventions_selftest_rules import rule_coverage_failures


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
    # NO SPACE after the semicolon. The arm required one, so this read as no
    # semicolon at all.
    ("The pane is blurred;the border is not.", "semicolon with no following space"),
    # AN UNPUNCTUATED em-dash splice, which is the shape of a JSON name or a short
    # label. Every other em-dash probe here ends in a period, and the title-separator
    # carve-out exempts anything with no sentence punctuation and five words a side,
    # so without this entry the word cap and the verb test are both unexercised.
    ("Fixed the crash — it was a null deref", "unpunctuated em-dash splice"),
    ("Blurs the pane — and lifts saturation", "unpunctuated em-dash splice opening with a conjunction"),
    # An unpunctuated splice that ONLY the word cap catches: no finite verb the list
    # knows, no leading conjunction, no sentence punctuation, and more than five words
    # a side. Without it the cap can be widened to 50 with everything still green,
    # because the two entries above are carried by the verb and conjunction tests.
    ("Blurs the scene behind the pane — a soft look that lifts saturation for the whole window",
     "unpunctuated em-dash splice caught only by the word cap"),
    # SENTENCE PUNCTUATION on one side, which is the carve-out's oldest test and was the
    # only one no probe exercised: both sides here are short, verbless and open with no
    # conjunction, so the word cap, the verb test and the conjunction test all pass it.
    ("Blurs the pane — softer edges. Nice.", "em-dash splice whose side carries sentence punctuation"),
    # THE ENTITY SPELLING. The arm looked for either spelling but the carve-out split on the
    # literal dash only, so an entity separator was reported and an entity splice was not
    # distinguishable from it. Both directions are pinned, here and in the OK list.
    ("Fixed the crash &mdash; it was a null deref", "em-dash splice written as an entity"),
    # The two NUMERIC references, which are the spellings that are well-formed XML. The
    # AppStream files declare no entities, so `&mdash;` there would be ill-formed and these
    # are what an author would actually end up with. Both went unnormalised.
    ("Fixed the crash &#8212; it was a null deref", "em-dash splice as a decimal reference"),
    ("Fixed the crash &#x2014; it was a null deref", "em-dash splice as a hex reference"),
    # A SPLICE WHOSE ONLY VERB IS ONE THE LIST DID NOT NAME. `stays` is used on 58 CHANGELOG
    # lines and 31 pack descriptions, and this exact sentence passed the gate before it was
    # added — the verb list under-catching is the detector's one failure direction, and this
    # is a measured instance of it rather than a hypothetical. It is also the only one of the
    # fourteen words added with it that survived: the other thirteen were pinned by nothing,
    # and ten of them were plural nouns that made genuine lists fail (see the OK entries and
    # _FINITE_VERBS' own comment).
    ("A radius of 0 now stays square at both ends; the band mitres the corner the way a "
     "drawing program would.", "semicolon whose left verb is 'stays'"),
    # A CAPITALISED leading verb, which is what conventions_prose._has_finite_verb's
    # .lower() is for (the detector moved out of the gate). Every
    # other probe's verbs are already lowercase, so the call was unpinned.
    ("Keeps the pack's default; the loader drops the entry", "semicolon with a capitalised leading verb"),
    # A RIGHT CLAUSE WHOSE FIRST WORD IS ITS VERB. Pins the slice bounds: consume that word
    # into the match and the right side reads as verbless, so the splice is exempted.
    ("The pane is blurred; is the border drawn over it?", "semicolon whose right clause opens with its verb"),
    # A SECOND CLAUSE OPENING WITH A BRACKET, which the arm's \w requirement skipped
    # entirely, so it read as no semicolon at all.
    ("The pane is blurred; (the border is not)", "semicolon whose second clause opens with a bracket"),
    # THREE parts to one separator string. CLAUDE.md allows a separator "between two nouns",
    # so a chain is outside it; this pins the exact-two test against being loosened.
    ("Alpha — Beta — Gamma", "two separators in one label"),
    # A SPLICE WITH SUB-THREE-WORD SIDES. The verb test works "at any length", which nothing
    # pinned: restoring the removed three-word clause floor left the suite green.
    ("Blur stops; border keeps", "splice whose clauses are two words each"),
]

SELFTEST_PROSE_OK = [
    # Genuine lists, exempt by the FINITE-VERB test and nothing else: the item after each
    # semicolon is a noun phrase with no verb the list knows, and one verbless side exempts
    # the construction. Neither of these pins an "enumeration signal" or a "three-word
    # clause floor" — an earlier version of this comment named both, and the rule implements
    # neither. The semicolon-count short-circuit is forbidden by the three-clause BAD probe
    # above, and the word floor was replaced by the verb test "at any length".
    "Sets the width, in pixels; the radius, in pixels; and the colour",
    "Left, top; right, bottom",
    # A verbless LEFT side beside a verb-carrying right one, which is the mirror of the
    # entries below and the half the both-sides test had no probe for: requiring a verb on
    # the right alone would flag this genuine list item.
    "Radius, in logical pixels; Strength, a unitless multiplier that keeps the bevel",
    # TWO-ITEM LISTS LONG ENOUGH TO CLEAR THE WORD FLOOR, which CLAUDE.md permits
    # without naming a minimum count and which only the finite-verb test lets
    # through. Neither item carries a verb; both carry the internal comma the
    # carve-out is written for. Drop the verb test and both are flagged.
    "Sets the width, in pixels; the radius, in logical pixels",
    "Radius, in logical pixels; Strength, a unitless multiplier",
    # GENUINE LISTS WHOSE ITEMS ARE PLURAL NOUNS THAT DOUBLE AS VERBS. These three were
    # findings for one round, because _FINITE_VERBS had been widened with `counts`, `names`,
    # `points`, `works`, `scales`, `treats`, `bends`, `fades`, `remains` and `wins` on the
    # since-retracted premise that the list could not produce a false positive. Each puts a
    # "finite verb" on BOTH sides of the semicolon while being a plain enumeration, so they
    # pin the narrowing: re-add any of those ten and these fail.
    "Corner names, as shown in the picker; tab counts, per column",
    "Sets the tab names, in order; the column counts, per strip",
    "Sets the bend counts, in pixels; the corner scales, in logical units",
    # A "#"-led line is a shell comment in pasteable terminal text, which CLAUDE.md
    # puts out of scope along with the rest of the code. Pins the skip.
    "Run it like this:\n# plasmazones --rules a - b\nThen restart.",
    # A literal separator between two nouns, which CLAUDE.md allows. The three below
    # it are the shapes that actually ship in this tree, so tightening the carve-out
    # cannot start flagging a real label without failing here first.
    "%1 — %2",
    "Column template — %1",
    # The same separator written as an entity has to stay exempt too, or the fix for the
    # BAD entity probes above could be a blanket flag. All three spellings.
    "%1 &mdash; %2",
    "%1 &#8212; %2",
    "%1 &#x2014; %2",
    "Phosphor Settings — Minimal Demo",
    "Phosphor.Registry — plugin demo + hot-reload",
    # A GENUINE LIST FOLLOWED BY A SENTENCE. Pins the per-segment split, and the order
    # matters: the verb that would falsely pair with the list has to sit in a DIFFERENT
    # sentence on the far side of the semicolon. Judged as one string, "Sets" before it
    # and "keeps" after it make both sides read as clauses and the list is flagged.
    # Segmented, the list's second item carries no verb and it passes. A leading
    # sentence instead of a trailing one does not pin anything, because the item after
    # the semicolon is still verbless either way.
    "Sets the width, in pixels; the radius, in logical pixels. It blurs the pane and keeps the border.",
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
# defect in consecutive review rounds, every one in code written to fix the
# previous one. Two patterns recurred and neither was reachable by any gate: a fix
# applied to ONE SIDE of a symmetric pair, and a coverage floor that counted the wrong
# thing four times. Both are exactly what a planted case catches
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
        if isinstance(raw, str):
            meta.write_text(raw, encoding="utf-8")
        else:
            meta.write_bytes(raw)
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


def _finite_verbs_failures() -> list[str]:
    """_FINITE_VERBS' membership, pinned against a literal in BOTH directions.

    The BAD and OK probes above pin the behaviour of a handful of words and nothing pins
    the SET. A round widened it by fourteen words on a premise that turned out to be false,
    and only three of the fourteen are caught by any probe: `stays` holds up a BAD probe,
    `counts` and `scales` each break an OK probe if re-added, and the other eleven can be
    added or removed with a green suite. Ten of the fourteen were plural nouns that made
    genuine comma-bearing lists into findings. A per-word probe cannot close that — it would
    need one probe per candidate word, written in advance. A literal can.

    Mirrors how _spdx_suffix_failures pins CODE_SUFFIXES, and for the same reason: neither
    direction is visible any other way, because the tree contains no prose that distinguishes
    them. A deliberate change edits this literal in the same commit and says why."""
    from conventions_prose import _FINITE_VERBS

    bad: list[str] = []
    expected = frozenset("""is are was were am be been being has have had do does did
        can cannot could will would shall should may might must
        isn't aren't wasn't weren't hasn't haven't doesn't don't didn't
        can't won't wouldn't shouldn't
        keeps drops sets reads writes runs takes gives makes shows uses needs holds
        adds stops starts applies returns means covers carries leaves gets goes comes
        sits lands falls picks sends pushes pulls draws paints binds clears
        stays allows""".split())
    for word in sorted(expected - _FINITE_VERBS):
        bad.append(f"_FINITE_VERBS no longer holds '{word}'")
    for word in sorted(_FINITE_VERBS - expected):
        bad.append(f"_FINITE_VERBS gained '{word}' without this selftest's literal being updated; if it is "
                   f"also a plural noun it can make a genuine list a finding")
    return bad


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
           True, "silent no-op")
    expect("packs that declare nothing at all",
           lambda d: [_pack(d, n, declare=False) for n in ("a", "b", "c")],
           True, "silent no-op")
    # THE CASE WHOSE ABSENCE LET A REGRESSION THROUGH. One declarer left after the id was
    # renamed on every other pack is a dead rule: one bucket, one holder, nothing compared.
    # The third floor caught it, the fourth dropped its threshold to zero and passed it, and
    # no case here noticed. The two siblings below are the same dead state reached by the
    # other two routes, a pack that declares nothing and a pack that will not parse.
    expect("one declarer left after the id was renamed on the rest",
           lambda d: [_pack(d, "kept")] + [_pack(d, n, param="roundBottomCornersNew")
                                           for n in ("a", "b", "c")],
           True, "silent no-op")
    expect("one declarer beside packs that declare nothing",
           lambda d: [_pack(d, "kept")] + [_pack(d, n, declare=False) for n in ("a", "b", "c")],
           True, "silent no-op")
    expect("one declarer beside packs that will not parse",
           lambda d: [_pack(d, "kept")] + [_pack(d, n, raw="{not json") for n in ("a", "b", "c")],
           True, "silent no-op")

    # `"description": null`. str()-ing it made the bucket key the literal word "None",
    # which counted as text and let an all-null set pass the floor. This is the one case
    # in this file's history that a written test caught before review did.
    expect("three packs whose description is JSON null",
           lambda d: [_pack(d, n, desc=None) for n in ("a", "b", "c")],
           True, "silent no-op")
    # And whitespace-only, which `not text` used to pass straight into the drift arms
    # so that " " was quoted back as a competing wording.
    expect("three packs whose description is a single space",
           lambda d: [_pack(d, n, desc="   ") for n in ("a", "b", "c")],
           True, "silent no-op")

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
    # THE MESSAGE ITSELF, not just its presence. The index and the quoted excerpt are the
    # whole point of these arms, and asserting only that one of three words appears left
    # five mutations alive — including swapping the truncation test, which makes BOTH arms
    # quote the empty string, the exact defect the arms were written to prevent.
    ref_text = "Rounds the bottom corners."
    short = "Rounds the"
    trunc = [m for _, m in run(lambda d: ([_pack(d, f"full{n}", desc=ref_text) for n in range(3)]
                                          + [_pack(d, "short", desc=short)]))]
    if not any("TRUNCATED at 0-based character 10" in m and "it should continue ' bottom corners.'" in m
               for m in trunc):
        bad.append(f"the TRUNCATED arm no longer reports the index and the missing tail: {trunc}")
    ext = [m for _, m in run(lambda d: ([_pack(d, f"short{n}", desc=short) for n in range(3)]
                                        + [_pack(d, "full", desc=ref_text)]))]
    if not any("EXTENDED from 0-based character 10" in m and "it adds ' bottom corners.'" in m for m in ext):
        bad.append(f"the EXTENDED arm no longer reports the index and the added tail: {ext}")
    # THE REFERENCE-HOLDERS SKIP. Remove `if text == ref: continue` and every pack holding
    # the reference gets a bogus "EXTENDED ... it adds ''" finding of its own. The agreeing
    # case returns before this loop, so nothing else notices.
    two = [m for _, m in run(lambda d: (_pack(d, "a"), _pack(d, "b"), _pack(d, "c", desc="Other.")))]
    if any("it adds ''" in m for m in two):
        bad.append(f"a pack holding the reference text was reported against itself: {two}")
    if len(two) != 1:
        bad.append(f"expected one finding for one drifting pack among three, got {len(two)}: {two}")
    # ONE ENTRY PER FILE, so a pack declaring the id twice cannot inflate the count the
    # message quotes for deciding which side is canonical.
    ref_twice = ('{"parameters": [{"id": "roundBottomCorners", "description": "Rounds the bottom corners."},'
                 ' {"id": "roundBottomCorners", "description": "Rounds the bottom corners."}]}')
    msgs = [m for _, m in run(lambda d: (_pack(d, "a"), _pack(d, "dup", raw=ref_twice),
                                         _pack(d, "z", desc="Other.")))]
    if not any("compared against the 2 pack(s)" in m for m in msgs):
        bad.append(f"a pack declaring the reference text twice inflated the reference tally: {msgs}")

    # A FORWARD GUARD, not coverage: that message no longer exists anywhere in the tree,
    # and this asserts the deleted arm does not come back with the defect it carried.
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
    # `parameters` wrong-typed. Only THREE of the four shapes actually reach the
    # isinstance(list) guard: null, a number and a BOOLEAN each raise TypeError when
    # iterated, which is the failure the guard prevents. An object and a string do NOT —
    # they iterate their keys and their characters, and `isinstance(p, dict)` then rejects
    # every element — so planting those two pinned nothing, and the boolean, which does
    # reach it, was the one shape missing. Both kept anyway: they cost nothing and they
    # document that the guard is not what makes them safe.
    for shape in ('{"parameters": null}', '{"parameters": 7}', '{"parameters": true}',
                  '{"parameters": {"a": 1}}', '{"parameters": "none"}'):
        expect(f"a pack whose parameters is {shape}",
               lambda d, s=shape: (_pack(d, "a"), _pack(d, "b"), _pack(d, "bad", raw=s)),
               False)
    expect("a pack whose root is an array",
           lambda d: (_pack(d, "a"), _pack(d, "b"), _pack(d, "bad", raw="[]")),
           False)
    expect("a pack with a non-string description",
           lambda d: (_pack(d, "a"), _pack(d, "b"), _pack(d, "num", desc=7)),
           True, "non-string")
    # A BOM'd pack must PARTICIPATE, not drop out: read as plain utf-8 json.loads raises,
    # and the ValueError arm then skips it silently. The BOM'd pack therefore has to
    # DISAGREE — an agreeing one was the first version of this case and it asserted
    # nothing, because "no findings" is also what a silently dropped pack produces, so
    # swapping utf-8-sig for utf-8 left the selftest green.
    expect("a BOM'd pack that disagrees",
           lambda d: (_pack(d, "a"),
                      _pack(d, "bom", raw='﻿{"parameters": [{"id": "roundBottomCorners", '
                                          '"description": "Rounds the bottom corners TOO."}]}')),
           True, "first differs")
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

    def symlinked_with_third(d):
        # The pair PLUS a pack that declares nothing. TWO dedups guard this rule and each
        # needs its own case: with the pair alone `matched` is 1 and the stale-glob arm fires,
        # so the TEXT-HOLDER dedup is never reached. With a third path matched is 2, and the
        # pair's two apparent holders are all that stands between the rule and silence.
        symlinked(d)
        _pack(d, "silent", declare=False)

    try:
        with tempfile.TemporaryDirectory() as probe:
            os.symlink(probe, Path(probe) / "l")
    except (OSError, NotImplementedError):
        pass
    else:
        expect("a pack symlinked to itself twice", symlinked, True, "stale")
        expect("a symlinked pair beside a non-declaring pack", symlinked_with_third, True,
               "silent no-op")

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
    # DEP5_HEAD_LINES' value, pinned to its literal, for the reason SIZE_CEILING and
    # JS_PRAGMA_WINDOW carry the same pin: the dangerous direction is invisible any other
    # way. Narrowing it to 2 left BOTH the selftest and a whole-tree run green while every
    # header whose tags sit on lines 3-8 silently stopped being read, so those files' licences
    # and holders went unchecked. NEITHER direction is tree-caught anywhere near the current
    # value: rule_dep5 over every tracked file reports zero findings for every value from 2 to
    # 21, first catches a widening at 22 (a scaffold fixture whose copyright line reads
    # "2026 <your name>") and reports two by 40. So this literal is the only thing holding the
    # value, in both directions. No file COUNT here on purpose — the first version of this
    # paragraph named one and the commit that wrote it added the file that falsified it, which
    # is the trap the note beside check-conventions.py's CODE_SUFFIXES records, with two stale
    # counts of its own. (The sweep bounds 2, 21, 22 and 40 above are just as falsifiable by a
    # single commit; they are kept because they are the measurement, not a tally.)
    # The sibling constant SPDX_HEAD_LINES had this closed a round earlier and this one was
    # missed in the same edit.
    if mod.DEP5_HEAD_LINES != 8:
        bad.append(f"DEP5_HEAD_LINES is {mod.DEP5_HEAD_LINES}, not 8; narrowing it silently stops the rule "
                   f"reading any header whose tags sit past the new bound")
    # Deliberately shaped like the real packaging/debian/copyright rather than flat: a
    # leading COMMENT, a multi-line Files list, and a CONTINUATION line on Copyright. The
    # flat version left the continuation arm, the comment skip, the final-stanza flush and
    # the trailer strip unreachable, and in the real file one holder of mesh_sim.h is
    # reachable only through a continuation line.
    stanza = (
        "# a comment the parser must skip\n"
        "Files: *\n"
        "Copyright: 2024-2026 fuddlesworth <fuddlesworth@users.noreply.github.com>\n"
        "License: GPL-3.0-or-later\n"
        "\n"
        "Files: libs/*\n"
        "  libs/extra/*\n"
        "Copyright: 2024-2026 fuddlesworth <fuddlesworth@users.noreply.github.com>\n"
        "  2008 Cedric Borgese <cedric@example.invalid>\n"
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
                if isinstance(body, bytes):
                    p.write_bytes(body)
                else:
                    p.write_text(body, encoding="utf-8")
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
    # THE RANGE AND PREFIX SPELLINGS, against a stanza naming the holder with NO year. The
    # loop above cannot pin them: its stanza blob is "2024-2026 fuddlesworth", so a regex
    # that strips only the first year leaves "-2026 fuddlesworth", which is still a
    # substring of that blob and the row passes anyway. Four regex mutations survived on
    # exactly that accident. With a year-less blob, any leftover year text fails the test.
    bare = "Files: *\nCopyright: fuddlesworth\nLicense: GPL-3.0-or-later\n"
    for spelling in ("2024-2026", "2024\u20132026", "2024-present", "2026, 2027",
                     "(c) 2026", "\u00a9 2026", "Copyright 2026", "(c)", "\u00a9", "Copyright"):
        got = run({"src/a.cpp": hdr(year=spelling)}, dep5=bare)
        if got:
            bad.append(f"dep5_problems fired on the holder spelling {spelling!r}: {got}")
    # CASE-INSENSITIVELY, which is what flags=re.IGNORECASE is for. Every spelling above
    # uses the exact case the pattern spells, so the flag itself was unpinned.
    # The COMBINED spelling, which stacks two prefixes and is the commonest of all. None of
    # these was covered, so a prefix that matched only once passed the suite while reporting
    # a holder the stanza plainly names.
    for spelling in ("COPYRIGHT 2026", "(C) 2026", "copyright 2026",
                     "Copyright (c) 2026", "Copyright (C) 2026", "Copyright \u00a9 2026",
                     "COPYRIGHT (C) 2026", "Copyright (c)",
                     # A comma before the NAME, and the prefix written with no space. The year
                     # run's own separator only consumes a comma followed by another year, so
                     # the first group kept a leading comma and reported it as the holder.
                     "2026,", "Copyright (c) 2026,", "Copyright (C) 2024-2026,",
                     "Copyright(c) 2026", "\u00a92026,",
                     # The sign directly after the word is the ONLY spelling the `\u00a9` in the
                     # lookahead class serves; every row above writes it with a space, so the
                     # class could be narrowed back with the suite still green.
                     "Copyright\u00a92026",
                     # An open-ended or spaced range with no second year, and the "by" form
                     # upstream headers are commonly pasted in with. Both left punctuation or
                     # a preposition at the front of the reported holder.
                     "2015-", "2026 -", "2026 by", "Copyright (c) 2026 by",
                     # A comma with NO year before it, which the year group's own trailing
                     # separator cannot reach. Either row alone covers the trailing-comma arm;
                     # the second keeps that cover if the first is ever removed, and adds the
                     # prefix-then-bare-comma sequence.
                     ",", "© ,"):
        got = run({"src/a.cpp": hdr(year=spelling)}, dep5=bare)
        if got:
            bad.append(f"dep5_problems fired on the holder spelling {spelling!r}: {got}")
    # And the strip must NOT eat the front of a name that merely begins with those letters.
    # ASSERT THE NAME AT THE FRONT OF THE MESSAGE, not merely somewhere inside it: without
    # the lookahead the prefix eats four letters and the rule still fires, on "eous Inc".
    # Both states produce a finding, so only the quoted name tells them apart — and a
    # CONTAINMENT test stopped doing that the round the finding began quoting the raw holder
    # as well, because "(the header says 'Copyrighteous Inc')" carries the needle whether the
    # strip ate the name or not. With that, the whole lookahead could be deleted and the
    # suite stayed green. startswith pins the stripped name and the absence of a raw-holder
    # suffix together.
    mangled = run({"src/a.cpp": hdr(year="", who="Copyrighteous Inc")}, dep5=bare)
    if not any(m.startswith("copyright holder 'Copyrighteous Inc' is not named") for m in mangled):
        bad.append(f"the prefix strip ate the front of a name that only starts with it: {mangled}")
    # The `by` arm has the same hazard as the prefix and needs the same lookahead: without one
    # it eats the first two letters of any name beginning with them. It must carry a YEAR to
    # reach that arm at all, since `by` sits inside the year group, so the message also carries
    # the raw-holder suffix and the assertion pins both halves.
    bystander = run({"src/a.cpp": hdr(who="Bystander Ltd")}, dep5=bare)
    if not any(m.startswith("copyright holder 'Bystander Ltd' (the header says '2026 Bystander Ltd')")
               for m in bystander):
        bad.append(f"the `by` strip ate the front of a name that only starts with it: {bystander}")
    # And the lookahead is NOT what protects a name whose first WORD is "By" — it guards `by`
    # as a word PREFIX only. What protects this is that the `by` arm sits inside the year
    # group, so it strips nothing when no year precedes it.
    byword = run({"src/a.cpp": hdr(year="", who="By The Way Inc")}, dep5=bare)
    if not any(m.startswith("copyright holder 'By The Way Inc' is not named") for m in byword):
        bad.append(f"the `by` arm ate the first word of a name that begins with it: {byword}")
    # The year run's LONE trailing separator also sits inside the year group, so a name that
    # opens with a hyphen and carries no year keeps it. Moving that group outside ate it, and
    # nothing failed.
    hyphen = run({"src/a.cpp": hdr(year="", who="-Acme Inc")}, dep5=bare)
    if not any(m.startswith("copyright holder '-Acme Inc' is not named") for m in hyphen):
        bad.append(f"the lone year separator ate a hyphen that opens a name: {hyphen}")
    # A holder that is NOTHING but a prefix word. The lookahead's `|$` branch consumes it and
    # the survivor is empty, and an empty name cannot be looked up in the stanza, so this used
    # to fall through in silence. Nothing else in the gate reads the holder, so a copyright
    # line naming nobody passed entirely.
    # Asserted on the QUOTED branch, not just on "names no holder": the message has two arms
    # and a containment test left the ", only <raw>" one pinned by nothing.
    nameless = run({"src/a.cpp": hdr(year="", who="Copyright")}, dep5=bare)
    if not any(m.startswith("copyright line names no holder, only 'Copyright'") for m in nameless):
        bad.append(f"a copyright line naming no holder was not reported: {nameless}")
    # An EMPTY tag value, which is what pins the capture to its own LINE. With `\s*` there the
    # capture crossed the newline and quoted the SPDX-License-Identifier line as the holder.
    empty = run({"src/a.cpp": "// SPDX-FileCopyrightText:\n// SPDX-License-Identifier: GPL-3.0-or-later\n"})
    if not any(m.startswith("copyright line names no holder at all") for m in empty):
        bad.append(f"an empty SPDX-FileCopyrightText value was not reported as holderless: {empty}")
    # The finding quotes the RAW holder beside the stripped name whenever the strip changed
    # it, so an author can search for the string their file actually holds. Nothing pinned
    # that arm: reverting it to the stripped name alone left the suite green.
    stranger = run({"src/a.cpp": hdr(who="Some Stranger")})
    if not any("'Some Stranger' (the header says '2026 Some Stranger')" in m for m in stranger):
        bad.append(f"the finding no longer quotes the raw holder beside the stripped name: {stranger}")
    # It still has to notice a holder the stanza really does not name.
    if not any("not named" in m for m in stranger):
        bad.append("dep5_problems missed a copyright holder absent from the stanza")
    # And the year strip must not swallow the whole holder when the name IS a year-like
    # token, nor fire when a holder legitimately carries a parenthesised project.
    if not any("not named" in m for m in run({"src/a.cpp": hdr(who="Simon Schneegans (Burn-My-Windows)")})):
        bad.append("dep5_problems missed an upstream holder absent from the stanza")

    # The licence split, in both directions, including last-match-wins: libs/* is
    # declared after * and governs.
    if not any("declares" in m for m in run({"src/a.cpp": hdr(lic="LGPL-2.1-or-later")})):
        bad.append("dep5_problems missed a header/stanza licence mismatch")
    # A SUBSTRING mismatch, which is the one an `in` test would miss. GPL-3.0 against a
    # GPL-3.0-or-later header is a real DEP-5 defect (the stanza under-declares the
    # licence), and the only mismatch planted above has neither string inside the other,
    # so relaxing `!=` to `not in` left the suite green.
    if not any("declares" in m for m in
               run({"src/a.cpp": hdr(lic="GPL-3.0-or-later")},
                   dep5="Files: *\nCopyright: 2026 fuddlesworth\nLicense: GPL-3.0\n")):
        bad.append("dep5_problems missed a stanza licence that is a substring of the header's")
    # THE MIRROR, because one direction of a substring pair is not the pair. Header inside
    # stanza is the other real defect: the HEADER under-declares. Pinning only the first
    # direction left `got not in s["license"]` passing.
    if not any("declares" in m for m in
               run({"src/a.cpp": hdr(lic="GPL-3.0")},
                   dep5="Files: *\nCopyright: 2026 fuddlesworth\nLicense: GPL-3.0-or-later\n")):
        bad.append("dep5_problems missed a header licence that is a substring of the stanza's")
    if run({"libs/phosphor-x/a.cpp": hdr(lic="LGPL-2.1-or-later")}):
        bad.append("dep5_problems ignored last-match-wins and used the first matching stanza")

    # A path no stanza covers is the drift this rule was written for.
    if not any("no Files stanza" in m for m in
               run({"src/a.cpp": hdr()}, dep5="Files: other/*\nCopyright: x\nLicense: MIT\n")):
        bad.append("dep5_problems missed a path that no Files stanza matches")

    # THE CONTINUATION LINES, both of them. `libs/extra/*` exists only on a continuation
    # of the Files list, and the second holder only on a continuation of Copyright, so a
    # parser that drops continuations matches the wrong stanza for the first and fails to
    # recognise the second. Reshaping the fixture reached those arms; only these assert on
    # them.
    cont = run({"libs/extra/a.cpp": hdr(lic="LGPL-2.1-or-later")})
    if cont:
        bad.append(f"a path listed on a continuation of Files did not match its stanza: {cont}")
    held = run({"libs/phosphor-x/a.cpp": hdr(who="Cedric Borgese", lic="LGPL-2.1-or-later")})
    if held:
        bad.append(f"a holder named on a continuation of Copyright was not recognised: {held}")
    # A COMMENT THAT LOOKS LIKE A FIELD. The fixture's plain comment cannot pin the skip,
    # because it carries no colon and the field regex rejects it anyway. This one would
    # open a bogus stanza and swallow every following line.
    commented = ("# Files: not-a-real-stanza\n"
                 "Files: *\n"
                 "Copyright: 2026 fuddlesworth\n"
                 "License: GPL-3.0-or-later\n")
    got = run({"src/a.cpp": hdr()}, dep5=commented)
    if got:
        bad.append(f"a commented-out Files line was parsed as a stanza: {got}")
    # A COMMENT THAT THE FIELD REGEX WOULD MATCH, which is the one shape whose removal
    # from the skip does harm. `# text` cannot match it (the regex wants `\S+:`, and the
    # space after # breaks that), so dropping the skip is inert for every one of the real
    # file's comments. `#Note:` DOES match, as a field named "#note", which sends the
    # parser down its else branch and clears `field` — so the CONTINUATION line after it
    # is dropped instead of joining Copyright, the blob loses that holder, and a header
    # naming it reports a violation that is not there.
    #
    # An INDENTED comment is deliberately not tested: under deb822 a leading space makes
    # the line a continuation, comment marker or not, so absorbing it is correct and the
    # skip is not meant to catch it.
    field_like = ("Files: *\n"
                  "Copyright: 2026 fuddlesworth\n"
                  "#Note: a comment the parser must not read as a field\n"
                  "  Some Stranger\n"
                  "License: GPL-3.0-or-later\n")
    got = run({"src/a.cpp": hdr(who="Some Stranger")}, dep5=field_like)
    if got:
        bad.append(f"a comment that looks like a field broke the continuation after it: {got}")
    # A STANZA WITH NO TRAILING NEWLINE, which is the only shape the final flush handles:
    # with one, the blank-line arm appends the stanza and the flush is dead code.
    noeol = "Files: *\nCopyright: 2026 fuddlesworth\nLicense: GPL-3.0-or-later"
    got = run({"src/a.cpp": hdr()}, dep5=noeol)
    if got:
        bad.append(f"the last stanza was dropped when the file had no trailing newline: {got}")
    # THE TRAILER STRIP. Sixty live headers close with ` -->` and five with `",`; NONE closes
    # with `*/`, which the strip covers as forward cover for a C block comment. No probe
    # produced any of the three, so the strip that removes them was unpinned.
    for tail in (" -->", " */", '",'):
        body = f"<!-- SPDX-FileCopyrightText: 2026 fuddlesworth{tail}\n// SPDX-License-Identifier: GPL-3.0-or-later\n"
        got = run({"src/a.cpp": body})
        if got:
            bad.append(f"a header whose holder ends in {tail!r} was not stripped before comparison: {got}")

    # Shapes it must pass over rather than choke on: a binary, a file with no SPDX
    # header at all (that is the spdx rule's finding, not this one), and an absent
    # DEP-5 file, which means the check cannot run rather than that everything failed.
    # The binary must carry SPDX bytes AFTER a NUL, or it passes for the wrong reason:
    # a plain PNG has no SPDX text, so the "no identifier, skip" arm handles it and
    # deleting the NUL test entirely leaves this green. Asserting on dep5_head directly
    # pins the test that is actually meant to fire.
    if run({"data/x.png": b"\x89PNG\r\n\x1a\n\x00SPDX-License-Identifier: MIT\n"}):
        bad.append("dep5_problems reported a binary asset whose bytes happen to spell an SPDX tag")
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        (d / "x.png").write_bytes(b"\x89PNG\x00SPDX-License-Identifier: MIT\n")
        if mod.dep5_head("x.png", d) is not None:
            bad.append("dep5_head returns a text head for a file containing a NUL byte")
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


def run_selftest(prose_problems, iter_json_prose, partition_readable) -> int:
    """The detectors and the precondition arrive as arguments, so this module never
    imports the gate at module scope and the pair cannot form a cycle.

    All three are REQUIRED. partition_readable used to default to None with the body
    guarding on it, so a caller that forgot it lost that coverage in silence rather
    than failing."""
    failures = []
    # Captured for the post-condition at the tail. SEVEN arms now redirect the gate's REPO
    # (and CODE_SUFFIXES, BASELINE, read_error or tracked_files with it) at a temp tree and
    # restore them in a finally, and several of them no longer run last. The post-condition is
    # what catches a dropped restore in any of them: a later arm would otherwise run against a
    # deleted directory and raise, which reads like a broken gate rather than a finding.
    # frozenset(), not the set itself: CODE_SUFFIXES is mutable, so binding a reference
    # would compare equal to itself after an arm mutated it IN PLACE, and the comparison
    # could never fire for the one shape nobody would notice.
    #
    # SIX gate globals, plus a table AND a function in two sibling modules. Each was added
    # after a mutation proved the suite green — or worse, red for the wrong reason — without
    # it. THREE are written by the arm they belong to (the wiring arm stubs `read_error` and
    # wraps `conventions_dep5._dead_stanza_problems`, the baseline-writer arm stubs
    # `tracked_files`) and `_SELECTED_RULES` by the main() the wiring arm drives. All four
    # matter beyond tidiness because a later arm inherits them: rule_license's no-identifier
    # deferral reads _SELECTED_RULES and a license arm runs AFTER the wiring arm, a leaked
    # tracked_files would hand every later rule a temp file list, and a leaked
    # _dead_stanza_problems makes the dead-stanza arm report TWO failures that both accuse the
    # check under test rather than the leak. conventions_shared_text.SHARED_PARAM_TEXT is
    # redirected by two arms in this file and is the same shape. All of them are compared here
    # rather than left to the arm that set them, because the arm that leaks is not the arm
    # that fails — which is also why none of these messages names a culprit.
    entry_globals = partition_readable.__globals__
    entry_repo, entry_suffixes = entry_globals["REPO"], frozenset(entry_globals["CODE_SUFFIXES"])
    entry_baseline = entry_globals["BASELINE"]
    entry_read_error = entry_globals["read_error"]
    entry_selected = frozenset(entry_globals["_SELECTED_RULES"])
    entry_tracked = entry_globals["tracked_files"]
    import conventions_dep5
    import conventions_shared_text

    entry_shared_text = conventions_shared_text.SHARED_PARAM_TEXT
    entry_dead_stanza = conventions_dep5._dead_stanza_problems

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

    # A malformed data JSON must be REPORTED, not passed over: returning quietly made it
    # indistinguishable from a file with no prose in it. The walker yields (None, message)
    # for that, which is the shape rule_prose turns into a finding.
    with tempfile.TemporaryDirectory() as d:
        broken = Path(d) / "broken.json"
        broken.write_text('{"description": ', encoding="utf-8")
        pairs = list(iter_json_prose(str(broken)))
        if not any(trail is None for trail, _ in pairs):
            failures.append(f"iter_json_prose passes over a malformed JSON file instead of reporting it: {pairs}")

    failures.extend(_finite_verbs_failures())
    failures.extend(_shared_text_failures())
    failures.extend(_dep5_failures())
    # And every fake-tree arm, which lives in its own module: the detector probes above
    # read no file and build no tree, while each of those drives a real rule over a temp
    # directory with the gate's REPO redirected at it. One call, so adding an arm there
    # needs no edit here.
    failures.extend(rule_coverage_failures(partition_readable))

    if entry_globals["REPO"] != entry_repo:
        failures.append(f"an arm left the gate's REPO at {entry_globals['REPO']}, not {entry_repo}")
    if entry_globals["CODE_SUFFIXES"] != entry_suffixes:
        failures.append("an arm left the gate's CODE_SUFFIXES changed")
    if entry_globals["BASELINE"] != entry_baseline:
        failures.append(f"an arm left the gate's BASELINE at {entry_globals['BASELINE']}, not {entry_baseline}")
    if entry_globals["read_error"] is not entry_read_error:
        failures.append("an arm left the gate's read_error stubbed, so every later rule reads through it")
    if frozenset(entry_globals["_SELECTED_RULES"]) != entry_selected:
        failures.append(f"an arm left the gate's _SELECTED_RULES as "
                        f"{sorted(entry_globals['_SELECTED_RULES'])}, not {sorted(entry_selected)}; "
                        f"rule_license's deferral arm reads it, so a later arm inherits the change")
    if entry_globals["tracked_files"] is not entry_tracked:
        failures.append("an arm left the gate's tracked_files stubbed, so every later rule and "
                        "main() would walk that arm's temp file list instead of the tree")
    if conventions_shared_text.SHARED_PARAM_TEXT is not entry_shared_text:
        failures.append("an arm left conventions_shared_text.SHARED_PARAM_TEXT redirected, so "
                        "rule_shared_param_text would check that arm's table instead of the real one")
    if conventions_dep5._dead_stanza_problems is not entry_dead_stanza:
        failures.append("an arm left conventions_dep5._dead_stanza_problems wrapped, so the dead-stanza "
                        "arm runs against a stubbed path lister and fails for a reason that has nothing "
                        "to do with the dead-stanza check")

    for line in failures:
        print(f"selftest: {line}", file=sys.stderr)
    if failures:
        print(f"\n{len(failures)} selftest failure(s)", file=sys.stderr)
        return 1
    print("selftest: ok")
    return 0
