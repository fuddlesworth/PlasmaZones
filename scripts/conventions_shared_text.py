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
job. There IS a live instance: a later pack's STORED value beats an earlier pack's
declared default, so "the one earliest in the chain wins" holds for two stored values
and inverts in the mixed case.

What the decline does not excuse is a rule that compares nothing. Zero coverage, not a
missing canon, is what actually went wrong the first time, so the coverage floor below is
the guard that reasoning implies and it is not optional. It took two goes to get right:
counting globbed FILES alone still passed on two unparseable packs, and on twenty that
parse while none declares the param. It now counts declarers too.
"""
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
    for param, pattern in SHARED_PARAM_TEXT.items():
        seen: dict[str, list[str]] = {}
        matched = 0
        for path in sorted(repo.glob(pattern)):
            matched += 1
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
                            f"comparison and no other rule reports it"))
                continue
            except (OSError, ValueError):
                # rule_prose reports a JSON SYNTAX error, including a BOM read as utf-8.
                # It does NOT report an unreadable file; its own read() raises on one.
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
                    rel = str(path.relative_to(repo))
                    holders = seen.setdefault(str(p.get("description", "")), [])
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
        # actually DECLARED it too. On this tree that is 20 of 24 globbed, so the floor has
        # 18 to spare and can only fire once the entry has gone stale, which is when it
        # should be deleted rather than trusted.
        declaring = {h for holders in seen.values() for h in holders}
        if matched < 2 or len(declaring) < 2:
            out.append((pattern, f"matched {matched} file(s), {len(declaring)} of them declaring "
                                 f"'{param}', so it was compared against nothing and this rule is "
                                 f"a silent no-op"))
            continue
        if len(seen) <= 1:
            continue
        # The most-held text is the reference. On a tie this is glob order, which is
        # arbitrary, so the message quotes BOTH sides and calls the winner the reference
        # rather than asserting the other is wrong. The gate fires either way, which is
        # the part that matters.
        ref = max(seen, key=lambda k: len(seen[k]))
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
