# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared-text consistency for a parameter the HOST resolves once per chain.

Split out of check-conventions.py for the reason its sibling conventions_selftest.py
records: the gate sits at the 1150-line ceiling, and this is a separable concern. It
depends on nothing but stdlib, returns plain tuples rather than the gate's `Violation`
so the two files cannot form a cycle, and is importable, which the rule was not while
it lived inline.

A control the host resolves once per chain must read the same way on every pack that
offers it, because the user meets one setting wearing many hats. Twenty surface packs
declare `roundBottomCorners`, and their descriptions drifted twice inside one audit,
once into a rule the resolver does not implement.

What this CANNOT catch, stated so nobody trusts it further than it goes: all twenty
being edited together into something that is consistent and wrong. That is the second
drift's actual shape, and no equality check reaches it. Pinning a canonical sentence
here was considered and declined, because it would pin whatever sentence was current at
the time, right or wrong, against a resolver this script cannot read — the very failure
it would look like it prevented — and it would need editing in lockstep with twenty data
files for every legitimate reword. Checking the sentence against the resolver is review's
job, and the resolver's order is worth stating here because it is easy to get backwards:
a usable STORED value on ANY pack returns immediately, and only an all-silent chain falls
back to the FIRST declarer's default. So a later pack's stored value does beat an earlier
pack's declared default. The shipped sentence, "where two packs have it set differently
the one earliest in the chain wins", is about two packs that have it SET, and for two
stored values earliest really does win, so it is accurate. An earlier version of this
docstring called it a live wrongness. It is not, and saying so invited a later round to
"fix" a sentence that was right.

What the decline does not excuse is a rule that compares nothing. Zero coverage, not a
missing canon, is what actually went wrong the first time, so the coverage floor below is
the guard that reasoning implies and it is not optional. It took five goes, and the four
wrong predicates are recorded AT THE FLOOR ITSELF rather than restated here. That is
deliberate: a second copy of the predicate in this docstring is what let the fourth
version read as intentional, since the copy still described the second version and nobody
reconciled them. One statement of it, beside the code, is the whole point.
"""
from __future__ import annotations

import json
from pathlib import Path

# param id -> glob of the metadata files that must agree on its description.
SHARED_PARAM_TEXT = {
    "roundBottomCorners": "plasmazones/data/surface/*/metadata.json",
}


def shared_param_problems(repo: Path) -> list[tuple[str, str]]:
    """Return (repo-relative path, message) for every pack whose shared text drifted.

    `repo` is passed in and every glob is anchored on it, NEVER on the process CWD:
    a CWD-relative glob makes this rule a silent no-op from any other directory, which
    is the one failure mode a consistency gate must not have.
    """
    out: list[tuple[str, str]] = []
    # An EMPTY table is the cheapest way to switch this rule off while leaving it
    # listed in --list-rules, so it cannot be the quiet path. Deleting the last entry
    # to silence a finding has to announce itself.
    if not SHARED_PARAM_TEXT:
        out.append(("scripts/conventions_shared_text.py",
                    "SHARED_PARAM_TEXT is empty, so this rule checks nothing; delete the "
                    "rule outright rather than emptying its table"))
    for param, pattern in SHARED_PARAM_TEXT.items():
        seen: dict[str, list[str]] = {}
        # Counted on the RESOLVED path. Two glob hits that are the same file through a
        # symlinked pack directory are one piece of evidence, and letting them count as
        # two would satisfy the floor below with no independent coverage at all.
        real: set[Path] = set()
        for path in sorted(repo.glob(pattern)):
            try:
                real.add(path.resolve())
            except OSError:
                real.add(path)
            try:
                # utf-8-sig so a BOM'd pack still PARTICIPATES. Decoding it as plain
                # utf-8 raises, and swallowing that below would drop the pack out of the
                # comparison silently, which for a majority holder also lowers the count
                # the message reports.
                doc = json.loads(path.read_text(encoding="utf-8-sig"))
            except UnicodeDecodeError as exc:
                # MUST precede the ValueError arm, being a subclass. Reported rather than
                # skipped because nothing else reports it: rule_prose reads with
                # errors="replace", and U+FFFD is legal inside a JSON string, so the file
                # parses there and looks fine.
                out.append((str(path.relative_to(repo)),
                            f"is not valid UTF-8 ({exc}), so it was dropped from the '{param}' "
                            f"comparison; no other rule reports it when the bad byte falls "
                            f"inside a JSON string"))
                continue
            except OSError as exc:
                # Reported, not skipped, and this is the arm's own mirror of the decode
                # one above. The gate's main() does drop an unreadable TRACKED path with
                # its own finding, but this rule globs rather than taking the file list,
                # so an untracked pack directory never passes through that filter. Saying
                # it twice for a tracked one is the cheaper mistake: the message here is
                # the one that names which comparison lost a participant.
                out.append((str(path.relative_to(repo)),
                            f"cannot be read ({exc.strerror or exc.__class__.__name__}), so it "
                            f"was dropped from the '{param}' comparison"))
                continue
            except ValueError:
                # rule_prose reports a JSON SYNTAX error, including a BOM read as utf-8,
                # so this one defers rather than duplicating it. THE DEFERRAL ASSUMES prose
                # is also selected, which it is on pre-commit and in CI but not under
                # `--rules shared-param-text` alone; there the dropped participant is named
                # by nothing. That is the one asymmetry with the OSError arm above, which
                # reports because nothing else would.
                continue
            if not isinstance(doc, dict):
                continue  # a root that parses but is not an object is not a pack
            # NOT `doc.get("parameters", [])`: the default only applies to an ABSENT key,
            # so `"parameters": null` from a half-finished edit reached the for-loop and
            # raised TypeError, which escaped the rule and took every OTHER rule in the
            # invocation down with it. A malformed pack must produce a finding, never a
            # traceback.
            params = doc.get("parameters")
            if not isinstance(params, list):
                continue
            for p in params:
                if isinstance(p, dict) and p.get("id") == param:
                    # A non-string description would otherwise be str()'d, so `null` reads
                    # as the literal word None in the message and sends the author hunting
                    # for it in the file. Schema-invalid, and guarded for the same reason
                    # the non-list `parameters` above is: a finding, never a traceback and
                    # never misleading prose.
                    desc = p.get("description")
                    if desc is None:
                        # Absent and JSON `null` both mean "says nothing", so both land in
                        # the empty bucket where the coverage floor can see them. str()-ing
                        # a null gave the bucket the literal key "None", which counted as
                        # text and let twenty all-null packs pass the floor.
                        desc = ""
                    elif not isinstance(desc, str):
                        out.append((str(path.relative_to(repo)),
                                    f"declares a non-string '{param}' description "
                                    f"({type(desc).__name__})"))
                        continue
                    elif not desc.strip():
                        # Whitespace-only says nothing either, and normalising it HERE
                        # rather than testing for it at each use is what keeps the
                        # reporting arms below consistent: they ask `not text`, which a
                        # single space passes, so " " would have been quoted back to the
                        # author as a genuine competing wording.
                        desc = ""
                    rel = str(path.relative_to(repo))
                    holders = seen.setdefault(desc, [])
                    # One entry per FILE. A pack declaring the id twice would otherwise
                    # inflate the count the author reads to decide which side is canonical.
                    if rel not in holders:
                        holders.append(rel)
        # COVERAGE FLOOR, on BOTH counts. A rule that compares nothing reports clean, which
        # is how the CWD-relative glob this function replaced stayed invisible: it found
        # zero packs from any directory but the repo root and passed. A mutation test is
        # not a ratchet, so the floor is.
        #
        # Counting globbed files ALONE was not enough, and the first version did only that:
        # two packs that both fail to parse, or twenty that parse while none declares the
        # param, both give matched >= 2 with nothing compared. The likelier real shape is
        # the second — rename the id across the packs, or mistype the key in
        # SHARED_PARAM_TEXT, and the rule goes quiet forever. So count the packs that
        # actually DECLARED it too.
        # Counting DECLARERS was still not enough, which is the third version of this
        # floor. Twenty packs can declare the id and every one omit the description: the
        # schema requires only id/name/type/default, so dropping it is schema-LEGAL and
        # the json-schema gate will not catch it either. All of them then land in the one
        # "" bucket, len(seen) == 1, and the rule goes quiet. So the floor has to reach
        # declared TEXT and not just declared ids.
        #
        # THE FOURTH VERSION WAS A REGRESSION, and this is the fifth. The third asked
        # "fewer than two packs carry text", which was one test doing two jobs, and it
        # threw away a real finding to do the second: one pack with text beside four that
        # declare the id and omit it is the drift this rule exists for, and the floor
        # swallowed it while reporting "compared against nothing" with two buckets in hand.
        # The fourth fixed that symptom by dropping the declarer threshold to ZERO, which
        # was the wrong predicate: it re-admitted three dead states the third had caught.
        # Rename the id on all but one pack, or leave one declarer beside packs that
        # declare nothing or fail to parse, and you get one bucket, one declarer, nothing
        # compared, and silence. Verified against both versions over the same fake trees.
        #
        # The question is neither "how many declare it" nor "how many carry text". It is
        # whether there is anything to COMPARE, and that is two buckets OR two text
        # holders, which is what `with_text` counts: HOLDERS OF TEXT, not declarers of the id,
# since a pack declaring it with an empty description says nothing. Naming that set for
# the declaration is how two earlier versions of this floor went wrong.
# One pack with text beside empty ones has two buckets, so it reports.
        # One pack with text and nothing else in the glob has one of each, so it fires.
        # Twenty agreeing have one bucket and twenty holders, so they stay silent.
        # Real tree: 24 globbed, 20 declaring, 20 with text, no empty bucket at all.
        matched = len(real)
        with_text = {h for text, holders in seen.items() if text for h in holders}
        if matched < 2:
            out.append((pattern, f"matched {matched} path(s), so there is nothing to compare and "
                                 f"this rule is a silent no-op; the glob has gone stale"))
            continue
        if len(seen) <= 1 and len(with_text) <= 1:
            out.append((pattern, f"matched {matched} path(s) and only {len(with_text)} carries a "
                                 f"'{param}' description, so nothing was compared and this rule is "
                                 f"a silent no-op; either the id is mistyped here or it has left "
                                 f"the packs"))
            continue
        if len(seen) <= 1:
            continue
        # The most-held text is the reference. On a tie this is glob order, which is
        # arbitrary, so the message quotes BOTH sides and calls the winner the reference
        # rather than asserting the other is wrong. The gate fires either way, which is
        # the part that matters.
        #
        # Chosen among the packs that actually say something. An omission is not a wording
        # and must not become the canon: with ten packs silent and two pairs disagreeing,
        # the empty bucket won on count, both real wordings were told they were the only
        # text in the tree, and the disagreement between them went unreported. That was the
        # fifth defect of one shape in this file — a fix applied to one side of a pair —
        # so this one is structural rather than another arm. Every empty holder now routes
        # to the `not text` arm below, and the arm that answered for an empty reference is
        # gone because no reference can be empty: reaching this line needs len(seen) >= 2,
        # and "" is a single key, so at most one bucket is empty and at least one is not.
        ref = max((t for t in seen if t), key=lambda k: len(seen[k]))
        for text, holders in sorted(seen.items()):
            if text == ref:
                continue
            # Report from the FIRST DIFFERING CHARACTER, and show BOTH sides there.
            # These descriptions run to several hundred characters and every real drift
            # so far was in a later sentence, so a leading excerpt quotes text that
            # looks correct and hides the thing it just detected.
            limit = min(len(text), len(ref))
            at = next((i for i in range(limit) if text[i] != ref[i]), limit)
            # Three shapes have no differing character at all, and a naive excerpt quotes
            # the empty string for one side of each. `at` lands at the shorter length, so
            # slicing the SHORTER side past its end gives ''. Name what changed instead.
            # This arm has to come first: an empty text gives limit == 0 and at == 0, which
            # satisfies the truncation test below and would be described as one.
            if not text:
                what = (f"declares no description, or an empty one; the reference text is "
                        f"{ref[:90]!r}")
            elif at == limit and len(text) < len(ref):
                what = (f"is the reference text TRUNCATED at 0-based character {at} "
                        f"({len(text)} chars against {len(ref)}); it should continue {ref[at:at + 90]!r}")
            elif at == limit:
                # The mirror of the truncation case: appending a sentence to one pack is
                # the likeliest authoring drift, and quoting ref[at:] for it asks the
                # reference for an index it does not have.
                what = (f"is the reference text EXTENDED from 0-based character {at} "
                        f"({len(text)} chars against {len(ref)}); it adds {text[at:at + 90]!r}")
            else:
                what = (f"first differs at 0-based character {at}: this pack has {text[at:at + 90]!r} "
                        f"where the reference has {ref[at:at + 90]!r}")
            # "reference", not "majority": on a tie there is no majority, and the count
            # below would read "the 1 packs".
            others = len(seen[ref])
            for holder in holders:
                out.append((holder, f"'{param}' description {what} (compared against the {others} pack(s) "
                                    f"holding the reference text)"))
    return out
