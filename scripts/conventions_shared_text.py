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
here was considered and declined, because it moves the prose into a GPL script that
then has to be edited in lockstep with twenty data files for every legitimate reword,
and it still would not check the sentence against the resolver. That is review's job.
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
        for path in sorted(repo.glob(pattern)):
            try:
                doc = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue  # rule_prose reports malformed JSON; an unreadable file is its own problem
            if not isinstance(doc, dict):
                continue  # a root that parses but is not an object is not a pack
            for p in doc.get("parameters", []):
                if isinstance(p, dict) and p.get("id") == param:
                    seen.setdefault(str(p.get("description", "")), []).append(str(path.relative_to(repo)))
        if len(seen) <= 1:
            continue
        # The most-held text is the reference. On a tie this is glob order, which is
        # arbitrary, so the message says which text it compared against rather than
        # claiming the other side is wrong. The gate fires either way, which is the
        # part that matters.
        ref = max(seen, key=lambda k: len(seen[k]))
        for text, holders in sorted(seen.items()):
            if text == ref:
                continue
            # Report from the FIRST DIFFERING CHARACTER. These descriptions run to
            # several hundred characters and both real drifts were in a later
            # sentence, so a leading excerpt quotes text that looks correct and hides
            # the thing it just detected.
            limit = min(len(text), len(ref))
            at = next((i for i in range(limit) if text[i] != ref[i]), limit)
            for holder in holders:
                out.append((holder, f"'{param}' description differs from the {len(seen[ref])} packs holding the "
                                    f"majority text, from character {at}: {text[at:at + 90]!r}"))
    return out
