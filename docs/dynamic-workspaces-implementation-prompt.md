<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# One-shot implementation prompt — Dynamic per-monitor workspaces (v3.5)

> Hand this to a fresh Claude Code session in the `dynamic-workspaces` worktree. Everything
> below the line is the task. It assumes `docs/dynamic-workspaces-plan.md` (the plan) and
> `docs/dynamic-workspaces-plan-prompt.md` (the spec behind it) exist beside it.

---

## PROMPT STARTS HERE

Implement **dynamic per-monitor workspaces** in PlasmaZones by executing
`docs/dynamic-workspaces-plan.md` — the complete, phased implementation plan. Read it in
full first, then `docs/dynamic-workspaces-plan-prompt.md` for the behavioral spec and
invariants behind it, then `CLAUDE.md`. Work until the feature is finished: all five
phases implemented, building, and passing tests. Do not stop early, do not defer work, do
not leave TODOs.

### Fidelity to the plan

- **The plan is the design. Implement it as written.** Do not redesign, "improve", or
  simplify architecture mid-flight. Where the plan gives a data model, wire format,
  algorithm sequence, state machine, config key, shortcut id, or file split, use exactly
  that.
- **Verify before you trust, adapt without redesigning.** Before using any seam the plan
  cites (file:line, symbol), read the current source. If reality has drifted, adapt the
  smallest necessary detail to reach the plan's stated intent and note the drift in your
  final report. If drift *invalidates a design decision* (not a detail), stop and ask the
  user rather than inventing a replacement.
- The plan's §11 is resolved: `PerOutputVirtualDesktops` applies live on `reconfigure()`
  (consent flow says immediate; disable collapses outputs to the active output's desktop),
  and the default bindings take over `Meta+Ctrl+Up/Down` as the §7 table says.

### When to stop and ask the user

ONLY for: a plan-invalidating drift or contradiction (two plan sections that cannot both
be satisfied); a decision the plan explicitly leaves to the user; or a destructive action.
Everything else — build errors, test failures, missing details the plan implies but
doesn't spell out — you resolve yourself, consistent with the plan and CLAUDE.md. Never
pause to ask "should I continue?" or to present a menu of options; pick the plan-faithful
option and proceed.

### Phase discipline (this is how "as few issues as possible" happens)

Execute phases 1→5 strictly in order. For EACH phase:
1. Re-read that phase's section of the plan plus the sections it references.
2. Implement it completely, including its tests (the plan's §9 test strategy names them).
3. Build: `cmake -B build -DBUILD_TESTING=ON` (first time) then
   `cmake --build build --parallel 6` — never a higher parallelism. Grep the build output
   for new warnings in changed files and fix them (no -Werror; warnings won't fail it).
4. Run `ctest --test-dir build --output-on-failure` YOURSELF, in the main session — never
   delegate ctest to a subagent. The test-time `glslangValidator` dependency must be on
   PATH. Fix every failure, including pre-existing tests your change broke.
5. Self-review the phase's diff against the plan section: every promised file present,
   every algorithm step implemented (not approximated), invariants from the spec's §1
   honored, DRY mandate honored (no duplicated component where the plan names a reusable
   one).
6. Commit the phase (conventional commit, e.g. `feat(workspaces): phase 1 — ownership map
   and stream`, SPDX headers on all new files per the CLAUDE.md licensing split). Do NOT
   push.
Do not start phase N+1 with phase N red.

### Correctness traps (from the plan's risk register — treat as checklists, not prose)

- **Echo safety first, not last.** Implement the pending-op ledger in the same commit as
  the first `createDesktop`/`removeDesktop`/`setCurrent` caller. Every KWin signal handler
  must consult it before reconciling. A snap-back that can re-trigger itself is a defect
  even if manual testing looks fine — write the loop unit test the plan requires.
- **Ints renumber.** Any `PlacementStateKey` or per-desktop map touched by
  create/remove/migrate goes through the shared renumber/reap pass — in ALL THREE engines
  (snap, tile, scroll). Grep for every per-desktop container before declaring the pass
  complete; the plan's enumeration is the starting list, not the proof.
- **Destroy-on-empty re-verifies emptiness at the last moment** and adopts-if-lost.
- **Sticky pins**: preserve the `ScreenContextTracker` contract; pins are re-keyed in the
  same pass, per the plan.
- **Change-gate every new signal**; emit only on actual change (repo-wide rule).
- **Qt6 string rules, `ConfigDefaults::` accessors for every key string, UUID braces
  convention (`toString()` except filesystem paths `WithoutBraces`), i18n via
  `PhosphorI18n::tr()` in C++ / `i18n()` in QML, plain prose for user-facing strings (the
  consent dialog and OSD hints included — no em-dash splices, no "Label: payload").**
- **File ceiling**: the plan pre-plans splits for the two big new files; honor them from
  the start rather than splitting after the fact.

### Tooling rules (hard-learned, follow exactly)

- Use the Edit/Write tools for file changes — never python/sed scripts to edit files.
- Never prefix Bash commands with `timeout`.
- Subagents may search/read; they never run ctest and never edit without your review.
- No `cmake --install`, no `sudo`, no pushes. Commits stay local to this worktree.

### Definition of done

All five phases committed; full build clean; full ctest green; `update-ts` target run
after the final tree state if any translatable strings were added; a final self-audit pass
re-reading the plan end to end and confirming (or fixing) every §6-contract item landed.
Then report: per-phase commit list, seam drift encountered, anything consciously deviated
from the plan and why, and any follow-up work the plan explicitly deferred.
